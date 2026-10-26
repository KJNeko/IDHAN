#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <future>
#include <memory>

#include "IDHANTypes.hpp"
#include "MappingHelpers.hpp"
#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"
#include "accumulateTagCounts.hpp"
#include "db/searchPath.hpp"

namespace idhan::test
{

static void flushTagCounts( pqxx::transaction_base& tx )
{
	accumulateTagCountsForTest( tx );
}

//! storage_count and display_count for the tag, or zeroes when the tag carries no row at all.
static std::pair< int, int > counts( pqxx::transaction_base& tx, const int tag_id, const int tag_domain_id )
{
	const auto result { tx.exec(
		"SELECT storage_count, display_count FROM tag_counts WHERE tag_id = $1 AND tag_domain_id = $2",
		pqxx::params { tag_id, tag_domain_id } ) };

	if ( result.empty() ) return { 0, 0 };

	return { result[ 0 ][ 0 ].as< int >(), result[ 0 ][ 1 ].as< int >() };
}

//! The row's physical version. An update rewrites it even when every value stays the same, so an unchanged
//! ctid is proof that nothing wrote to the row.
static std::string countsVersion( pqxx::transaction_base& tx, const int tag_id, const int tag_domain_id )
{
	return tx.query_value< std::string >(
		"SELECT ctid::text FROM tag_counts WHERE tag_id = $1 AND tag_domain_id = $2",
		pqxx::params { tag_id, tag_domain_id } );
}

SCENARIO_METHOD( MigratedSchema, "Tag counts", "[db][tags][counts]" )
{
	pqxx::work tx { connection() };

	GIVEN( "two characters under a series under a copyright, and a mapped tag that is aliased away" )
	{
		const auto domain { insertDomain( tx, "characters" ) };
		const auto record { insertRecord( tx, 1 ) };

		const auto ahri { insertTag( tx, "character", "ahri" ) };
		const auto kindred { insertTag( tx, "character", "kindred" ) };
		const auto league { insertTag( tx, "series", "league of legends" ) };
		const auto riot { insertTag( tx, "copyright", "riot games" ) };
		const auto vi { insertTag( tx, "character", "vi" ) };
		const auto violet { insertTag( tx, "character", "violet" ) };

		insertParent( tx, domain, league, ahri );
		insertParent( tx, domain, league, kindred );
		insertParent( tx, domain, riot, league );

		insertMapping( tx, record, ahri, domain );
		insertMapping( tx, record, kindred, domain );
		insertMapping( tx, record, vi, domain );
		insertAlias( tx, domain, vi, violet );
		flushTagCounts( tx );

		WHEN( "every mapping stands" )
		{
			THEN( "storage counts the tag as it was applied, with no parent or alias in it" )
			{
				CHECK( counts( tx, ahri, domain ).first == 1 );
				CHECK( counts( tx, kindred, domain ).first == 1 );
				CHECK( counts( tx, vi, domain ).first == 1 );

				CHECK( counts( tx, league, domain ).first == 0 );
				CHECK( counts( tx, riot, domain ).first == 0 );
				CHECK( counts( tx, violet, domain ).first == 0 );
			}

			THEN( "display counts what the record resolves to, parents and aliases included" )
			{
				CHECK( counts( tx, ahri, domain ).second == 1 );
				CHECK( counts( tx, kindred, domain ).second == 1 );
				CHECK( counts( tx, league, domain ).second == 2 );
				CHECK( counts( tx, riot, domain ).second == 1 );

				CHECK( counts( tx, vi, domain ).second == 0 );
				CHECK( counts( tx, violet, domain ).second == 1 );
			}
		}

		WHEN( "a mapping is written without changing what it resolves to" )
		{
			const auto before { countsVersion( tx, ahri, domain ) };

			tx.exec( "UPDATE active_tag_mappings SET ideal_tag_id = ideal_tag_id" );

			THEN( "the counts are left alone rather than decremented and incremented again" )
			{
				CHECK( countsVersion( tx, ahri, domain ) == before );
				CHECK( counts( tx, ahri, domain ) == std::pair { 1, 1 } );
			}
		}

		WHEN( "one character is removed" )
		{
			removeMapping( tx, record, ahri, domain );
			flushTagCounts( tx );

			THEN( "only what it was holding up is taken off the counts" )
			{
				CHECK( counts( tx, ahri, domain ) == std::pair { 0, 0 } );
				CHECK( counts( tx, kindred, domain ) == std::pair { 1, 1 } );
				CHECK( counts( tx, league, domain ) == std::pair { 0, 1 } );
				CHECK( counts( tx, riot, domain ) == std::pair { 0, 1 } );
			}
		}

		WHEN( "every mapping is removed" )
		{
			removeMapping( tx, record, ahri, domain );
			flushTagCounts( tx );
			removeMapping( tx, record, kindred, domain );
			removeMapping( tx, record, vi, domain );
			flushTagCounts( tx );

			THEN( "nothing is left counted" )
			{
				CHECK(
					tx.query_value< int >(
						"SELECT COUNT(*) FROM tag_counts WHERE storage_count <> 0 OR display_count <> 0" )
				    == 0 );
			}
		}
	}
}

static std::unique_ptr< pqxx::connection > tagCountConnection( const std::string& schema )
{
	auto other { std::make_unique< pqxx::connection >( connectionString() ) };
	pqxx::nontransaction tx { *other };
	tx.exec( "SET search_path = " + db::makeSearchPath( schema ) );
	tx.exec( "SET statement_timeout = '5s'" );
	return other;
}

static void commitTagCountTransaction( pqxx::work& tx )
{
	tx.commit();
}

static void checkTagCountsMatchMappings( pqxx::transaction_base& tx )
{
	CHECK( tx.query_value< int >( R"(
        WITH expected AS (
            SELECT tag_id, tag_domain_id, SUM(storage_count) AS storage_count, SUM(display_count) AS display_count
            FROM (
                SELECT tag_id, tag_domain_id, COUNT(*) AS storage_count, 0 AS display_count
                FROM active_tag_mappings GROUP BY tag_domain_id, tag_id
                UNION ALL
                SELECT tag_id, tag_domain_id, 0, COUNT(*)
                FROM active_tag_mappings_final GROUP BY tag_domain_id, tag_id
            ) contributions GROUP BY tag_domain_id, tag_id
        )
        SELECT COUNT(*) FROM expected e FULL JOIN tag_counts c USING (tag_id, tag_domain_id)
        WHERE COALESCE(e.storage_count, 0) <> COALESCE(c.storage_count, 0)
            OR COALESCE(e.display_count, 0) <> COALESCE(c.display_count, 0)
    )" ) == 0 );
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Bulk mappings resolve ideals immediately and leave grouped committed count deltas",
	"[db][tags][counts][queued]" )
{
	pqxx::work tx { connection() };
	const auto domain { insertDomain( tx, "bulk" ) };
	const auto source { insertTag( tx, "", "source" ) };
	const auto middle { insertTag( tx, "", "middle" ) };
	const auto target { insertTag( tx, "", "target" ) };
	insertAlias( tx, domain, source, middle );
	insertAlias( tx, domain, middle, target );
	for ( int seed { 1 }; seed <= 64; ++seed ) insertRecord( tx, seed );
	tx.exec(
		"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
		"SELECT record_id, $1, $2 FROM records",
		pqxx::params { source, domain } );

	CHECK(
		tx.query_value< int >(
			"SELECT COUNT(*) FROM active_tag_mappings WHERE ideal_tag_id = $1", pqxx::params { target } )
	    == 64 );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
	tx.commit();

	pqxx::nontransaction read { connection() };
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
	flushTagCounts( read );
	CHECK( counts( read, source, domain ) == std::pair { 64, 0 } );
	CHECK( counts( read, middle, domain ) == std::pair { 0, 0 } );
	CHECK( counts( read, target, domain ) == std::pair { 0, 64 } );
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0 );
	checkTagCountsMatchMappings( read );
}

TEST_CASE_METHOD( MigratedSchema, "Queued count deltas cancel and respect savepoints", "[db][tags][counts][queued]" )
{
	pqxx::work tx { connection() };
	const auto domain { insertDomain( tx, "savepoints" ) };
	const auto tag { insertTag( tx, "", "tag" ) };
	const auto first { insertRecord( tx, 1 ) };
	const auto second { insertRecord( tx, 2 ) };
	insertMapping( tx, first, tag, domain );
	removeMapping( tx, first, tag, domain );
	flushTagCounts( tx );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );

	tx.exec( "SAVEPOINT discarded" );
	insertMapping( tx, first, tag, domain );
	tx.exec( "ROLLBACK TO discarded" );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0 );
	insertMapping( tx, second, tag, domain );
	tx.exec( "SAVEPOINT kept" );
	insertMapping( tx, first, tag, domain );
	tx.exec( "ROLLBACK TO kept" );
	flushTagCounts( tx );
	CHECK( counts( tx, tag, domain ) == std::pair { 1, 1 } );
	// More changes remain pending until the next accumulation pass.
	insertMapping( tx, first, tag, domain );
	CHECK( counts( tx, tag, domain ) == std::pair { 1, 1 } );
	flushTagCounts( tx );
	CHECK( counts( tx, tag, domain ) == std::pair { 2, 2 } );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0 );
	removeMapping( tx, second, tag, domain );
	flushTagCounts( tx );
	tx.commit();
	pqxx::nontransaction read { connection() };
	CHECK( counts( read, tag, domain ) == std::pair { 1, 1 } );
	checkTagCountsMatchMappings( read );
}

TEST_CASE_METHOD(
	MigratedSchema,
	"A count accumulation failure preserves committed mappings and queued deltas for retry",
	"[db][tags][counts][queued]" )
{
	TagDomainID domain {};
	TagID source {}, target {};
	RecordID record {};
	{
		pqxx::work setup { connection() };
		domain = insertDomain( setup, "failure" );
		source = insertTag( setup, "", "source" );
		target = insertTag( setup, "", "target" );
		record = insertRecord( setup, 1 );
		insertAlias( setup, domain, source, target );
		setup.exec(
			"CREATE FUNCTION reject_display_count() RETURNS trigger LANGUAGE plpgsql AS $$ "
			"BEGIN IF NEW.display_count <> 0 THEN RAISE EXCEPTION 'injected count failure'; END IF; "
			"RETURN NEW; END $$; "
			"CREATE TRIGGER reject_display_count BEFORE INSERT OR UPDATE ON tag_counts "
			"FOR EACH ROW EXECUTE FUNCTION reject_display_count()" );
		setup.commit();
	}
	{
		pqxx::work writer { connection() };
		insertMapping( writer, record, source, domain );
		writer.commit();
	}
	{
		pqxx::work failed { connection() };
		CHECK_THROWS_AS( flushTagCounts( failed ), pqxx::sql_error );
	}
	{
		pqxx::work retry { connection() };
		CHECK( retry.query_value< int >( "SELECT COUNT(*) FROM tag_mappings" ) == 1 );
		CHECK( retry.query_value< int >( "SELECT COUNT(*) FROM active_tag_mappings" ) == 1 );
		CHECK( retry.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
		CHECK( retry.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
		retry.exec( "DROP TRIGGER reject_display_count ON tag_counts" );
		flushTagCounts( retry );
		retry.commit();
	}
	pqxx::nontransaction read { connection() };
	CHECK( counts( read, source, domain ) == std::pair { 1, 0 } );
	CHECK( counts( read, target, domain ) == std::pair { 0, 1 } );
	checkTagCountsMatchMappings( read );
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Concurrent count transactions append independently and combine without lost updates",
	"[db][tags][counts][queued]" )
{
	TagDomainID domain {};
	TagID a {}, b {};
	RecordID first {}, second {};
	{
		pqxx::work setup { connection() };
		domain = insertDomain( setup, "concurrent" );
		a = insertTag( setup, "", "a" );
		b = insertTag( setup, "", "b" );
		first = insertRecord( setup, 1 );
		second = insertRecord( setup, 2 );
		setup.commit();
	}
	auto other { tagCountConnection( schema() ) };
	pqxx::work left { connection() };
	pqxx::work right { *other };
	left.exec( "SET LOCAL statement_timeout = '5s'" );
	// Opposite append order must not affect the accumulated totals.
	insertMapping( left, first, a, domain );
	insertMapping( left, first, b, domain );
	insertMapping( right, second, b, domain );
	insertMapping( right, second, a, domain );
	CHECK( left.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
	CHECK( right.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
	CHECK( right.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
	auto committed { std::async( std::launch::async, commitTagCountTransaction, std::ref( left ) ) };
	right.commit();
	committed.get();
	pqxx::nontransaction read { connection() };
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 4 );
	flushTagCounts( read );
	CHECK( counts( read, a, domain ) == std::pair { 2, 2 } );
	CHECK( counts( read, b, domain ) == std::pair { 2, 2 } );
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0 );
	checkTagCountsMatchMappings( read );
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Merged inherited aliases count only the inherited rows actually retained",
	"[db][tags][counts][queued]" )
{
	pqxx::work tx { connection() };
	const auto domain { insertDomain( tx, "parents" ) };
	const auto record { insertRecord( tx, 1 ) };
	const auto child { insertTag( tx, "", "child" ) };
	const auto parent { insertTag( tx, "", "parent" ) };
	const auto a { insertTag( tx, "", "a" ) };
	const auto b { insertTag( tx, "", "b" ) };
	const auto target { insertTag( tx, "", "target" ) };
	insertParent( tx, domain, parent, child );
	insertParent( tx, domain, a, parent );
	insertParent( tx, domain, b, parent );
	const bool existing_target { GENERATE( false, true ) };
	if ( existing_target ) insertParent( tx, domain, target, parent );
	insertMapping( tx, record, child, domain );
	flushTagCounts( tx );
	checkTagCountsMatchMappings( tx );
	tx.exec(
		"INSERT INTO tag_aliases (aliased_id, alias_id, tag_domain_id) "
		"VALUES ($1, $3, $4), ($2, $3, $4)",
		pqxx::params { a, b, target, domain } );
	CHECK( finalTags( tx, record, domain ) == sorted( { child, parent, target } ) );
	flushTagCounts( tx );
	CHECK( counts( tx, a, domain ) == std::pair { 0, 0 } );
	CHECK( counts( tx, b, domain ) == std::pair { 0, 0 } );
	CHECK( counts( tx, target, domain ) == std::pair { 0, 1 } );
	checkTagCountsMatchMappings( tx );
	removeMapping( tx, record, child, domain );
	flushTagCounts( tx );
	checkTagCountsMatchMappings( tx );
	tx.commit();
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Direct partition updates and cascaded deletes retain correct counts",
	"[db][tags][counts][queued]" )
{
	pqxx::work tx { connection() };
	const bool existing_partition { GENERATE( false, true ) };
	const auto domain { existing_partition ? tx.query_value< int >( "SELECT MIN(tag_domain_id) FROM tag_domains" ) :
	                                         insertDomain( tx, "partitions" ) };
	const auto record { insertRecord( tx, 1 ) };
	const auto source { insertTag( tx, "", "source" ) };
	const auto target { insertTag( tx, "", "target" ) };
	insertMapping( tx, record, source, domain );
	flushTagCounts( tx );
	const auto partition { tx.quote_name( "active_tag_mappings_domain_" + std::to_string( domain ) ) };
	tx.exec( "UPDATE " + partition + " SET ideal_tag_id = $1", pqxx::params { target } );
	flushTagCounts( tx );
	checkTagCountsMatchMappings( tx );
	removeMapping( tx, record, source, domain );
	flushTagCounts( tx );
	CHECK( counts( tx, source, domain ) == std::pair { 0, 0 } );
	CHECK( counts( tx, target, domain ) == std::pair { 0, 0 } );
	checkTagCountsMatchMappings( tx );
	tx.commit();
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Deleting a domain discards committed counters and pending deltas safely",
	"[db][tags][counts][queued]" )
{
	pqxx::work tx { connection() };
	const auto first_domain { insertDomain( tx, "first" ) };
	const auto second_domain { insertDomain( tx, "second" ) };
	const auto record { insertRecord( tx, 1 ) };
	const auto tag { insertTag( tx, "", "tag" ) };
	insertMapping( tx, record, tag, first_domain );
	insertMapping( tx, record, tag, second_domain );
	flushTagCounts( tx );
	tx.exec( "DELETE FROM tag_domains WHERE tag_domain_id = $1", pqxx::params { first_domain } );
	removeMapping( tx, record, tag, second_domain );
	insertMapping( tx, record, tag, second_domain );
	tx.exec( "DELETE FROM tag_domains WHERE tag_domain_id = $1", pqxx::params { second_domain } );
	tx.commit();
	pqxx::nontransaction read { connection() };
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
	CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0 );
}

} // namespace idhan::test
