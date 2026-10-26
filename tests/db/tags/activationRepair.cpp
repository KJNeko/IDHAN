#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "MappingHelpers.hpp"
#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"
#include "accumulateTagCounts.hpp"

namespace idhan::test
{

static std::string tagActivationRepairSql()
{
	const auto path { std::filesystem::path { IDHAN_SOURCE_DIR }
		              / "IDHANMigration/src/func_tag_mappings_after_active_insert/432-repair.sql" };
	std::ifstream input { path };
	if ( !input ) throw std::runtime_error( "Could not read tag activation repair migration" );

	return { std::istreambuf_iterator< char > { input }, std::istreambuf_iterator< char > {} };
}

static void checkActivationCounts( pqxx::transaction_base& tx )
{
	CHECK( tx.query_value< int >( R"(
        WITH expected AS (
            SELECT tag_id, tag_domain_id, SUM(storage_count) AS storage_count, SUM(display_count) AS display_count
            FROM (
                SELECT tag_id, tag_domain_id, COUNT(*) AS storage_count, 0 AS display_count
                FROM active_tag_mappings GROUP BY tag_id, tag_domain_id
                UNION ALL
                SELECT tag_id, tag_domain_id, 0, COUNT(*)
                FROM active_tag_mappings_final GROUP BY tag_id, tag_domain_id
            ) contributions GROUP BY tag_id, tag_domain_id
        )
        SELECT COUNT(*) FROM expected e FULL JOIN tag_counts c USING (tag_id, tag_domain_id)
        WHERE COALESCE(e.storage_count, 0) <> COALESCE(c.storage_count, 0)
           OR COALESCE(e.display_count, 0) <> COALESCE(c.display_count, 0)
    )" ) == 0 );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 0 );
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Tag activation migration repairs missing mappings, inherited parents and stale counts",
	"[db][tags][activation-repair]" )
{
	pqxx::work tx { connection() };
	const auto first_domain { insertDomain( tx, "gelbooru" ) };
	const auto second_domain { insertDomain( tx, "pixiv" ) };
	const auto source { insertTag( tx, "", "source" ) };
	const auto first_ideal { insertTag( tx, "", "first ideal" ) };
	const auto second_ideal { insertTag( tx, "", "second ideal" ) };
	const auto parent { insertTag( tx, "", "parent" ) };
	const auto grandparent { insertTag( tx, "", "grandparent" ) };
	const auto orphan { insertTag( tx, "", "orphan" ) };
	insertAlias( tx, first_domain, source, first_ideal );
	insertAlias( tx, second_domain, source, second_ideal );
	insertParent( tx, first_domain, parent, first_ideal );
	insertParent( tx, first_domain, grandparent, parent );
	const auto record { insertRecord( tx, 1 ) };
	const auto without_file { insertRecordWithoutFile( tx, 2 ) };
	insertMapping( tx, record, source, first_domain );
	insertMapping( tx, record, source, second_domain );
	insertMapping( tx, without_file, source, first_domain );
	REQUIRE( finalTags( tx, record, first_domain ) == sorted( { first_ideal, parent, grandparent } ) );

	SECTION( "Both active tables were truncated" )
	{
		tx.exec( "TRUNCATE active_tag_mappings, active_tag_mappings_parents" );
	}
	SECTION( "Only inherited parents were truncated" )
	{
		tx.exec( "TRUNCATE active_tag_mappings_parents" );
	}
	SECTION( "Only active mappings were truncated, leaving stale parents" )
	{
		tx.exec( "TRUNCATE active_tag_mappings" );
	}

	tx.exec(
		"INSERT INTO active_tag_mappings_parents (record_id, tag_id, origin_id, tag_domain_id) VALUES ($1, $2, $3, $4)",
		pqxx::params { record, orphan, source, first_domain } );
	tx.exec(
		"INSERT INTO tag_counts (tag_id, tag_domain_id, storage_count, display_count) VALUES ($1, $2, 99, 99) "
		"ON CONFLICT (tag_id, tag_domain_id) DO UPDATE SET storage_count = 99, display_count = 99",
		pqxx::params { orphan, first_domain } );
	tx.exec(
		"INSERT INTO tag_count_delta (tag_id, tag_domain_id, storage_delta, display_delta) VALUES ($1, $2, 999, 999)",
		pqxx::params { orphan, first_domain } );
	tx.exec( "DROP TRIGGER trg_tag_mappings_after_active_insert ON file_info" );
	tx.exec( "DROP TRIGGER trg_tag_mappings_after_insert ON tag_mappings" );

	const auto migration { tagActivationRepairSql() };
	tx.exec( migration );
	CHECK( tx.query_value< int >( R"(
        SELECT COUNT(*) FROM pg_trigger WHERE NOT tgisinternal AND tgenabled = 'O'
          AND ((tgrelid = 'file_info'::regclass AND tgname = 'trg_tag_mappings_after_active_insert')
            OR (tgrelid = 'tag_mappings'::regclass AND tgname = 'trg_tag_mappings_after_insert'))
    )" ) == 2 );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_mappings" ) == 3 );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM file_info" ) == 1 );
	CHECK( finalTags( tx, record, first_domain ) == sorted( { first_ideal, parent, grandparent } ) );
	CHECK( finalTags( tx, record, second_domain ) == std::vector { second_ideal } );
	CHECK( finalTags( tx, without_file, first_domain ).empty() );
	CHECK(
		parentRows( tx, record, first_domain )
	    == std::vector { ParentRow { parent, first_ideal, 0 }, ParentRow { grandparent, parent, 1 } } );
	CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_counts WHERE tag_id = $1", pqxx::params { orphan } ) == 0 );
	checkActivationCounts( tx );

	// Re-running the repair must not double inherited contributions or replay stale deltas.
	tx.exec( migration );
	CHECK( finalTags( tx, record, first_domain ) == sorted( { first_ideal, parent, grandparent } ) );
	CHECK(
		parentRows( tx, record, first_domain )
	    == std::vector { ParentRow { parent, first_ideal, 0 }, ParentRow { grandparent, parent, 1 } } );
	accumulateTagCountsForTest( tx );
	checkActivationCounts( tx );
}

TEST_CASE_METHOD(
	MigratedSchema,
	"Tag activation works for both arrival orders and bulk file inserts in separate domains",
	"[db][tags][activation-repair]" )
{
	pqxx::work tx { connection() };
	const auto first_domain { insertDomain( tx, "first" ) };
	const auto second_domain { insertDomain( tx, "second" ) };
	const auto source { insertTag( tx, "", "source" ) };
	const auto first_ideal { insertTag( tx, "", "first ideal" ) };
	const auto second_ideal { insertTag( tx, "", "second ideal" ) };
	const auto parent { insertTag( tx, "", "parent" ) };
	const auto plain { insertTag( tx, "", "plain" ) };
	insertAlias( tx, first_domain, source, first_ideal );
	insertAlias( tx, second_domain, source, second_ideal );
	insertParent( tx, first_domain, parent, first_ideal );

	const auto file_first { insertRecord( tx, 1 ) };
	insertMapping( tx, file_first, source, first_domain );
	insertMapping( tx, file_first, source, second_domain );
	CHECK( finalTags( tx, file_first, first_domain ) == sorted( { first_ideal, parent } ) );
	CHECK( finalTags( tx, file_first, second_domain ) == std::vector { second_ideal } );

	const auto tags_first { insertRecordWithoutFile( tx, 2 ) };
	const auto another { insertRecordWithoutFile( tx, 3 ) };
	const auto without_file { insertRecordWithoutFile( tx, 4 ) };
	insertMapping( tx, tags_first, source, first_domain );
	insertMapping( tx, tags_first, source, second_domain );
	insertMapping( tx, another, plain, first_domain );
	insertMapping( tx, without_file, source, first_domain );
	CHECK( finalTags( tx, tags_first, first_domain ).empty() );
	tx.exec(
		"INSERT INTO file_info (record_id, size, extension) VALUES ($1, 0, 'png'), ($2, 0, 'png')",
		pqxx::params { tags_first, another } );
	CHECK( finalTags( tx, tags_first, first_domain ) == sorted( { first_ideal, parent } ) );
	CHECK( finalTags( tx, tags_first, second_domain ) == std::vector { second_ideal } );
	CHECK( finalTags( tx, another, first_domain ) == std::vector { plain } );
	CHECK( finalTags( tx, without_file, first_domain ).empty() );
	accumulateTagCountsForTest( tx );
	checkActivationCounts( tx );

	tx.exec(
		"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
		pqxx::params { tags_first, source, first_domain } );
	tx.exec(
		"INSERT INTO file_info (record_id, size, extension) VALUES ($1, 0, 'png') ON CONFLICT DO NOTHING",
		pqxx::params { tags_first } );
	CHECK( parentRows( tx, tags_first, first_domain ) == std::vector { ParentRow { parent, first_ideal, 0 } } );
	accumulateTagCountsForTest( tx );
	checkActivationCounts( tx );
}

} // namespace idhan::test
