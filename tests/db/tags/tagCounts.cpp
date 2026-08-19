#include <catch2/catch_test_macros.hpp>

#include "MappingHelpers.hpp"
#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

//! storage_count and display_count for the tag, or zeroes when the tag carries no row at all.
static std::pair< int, int > counts( pqxx::transaction_base& tx, const int tag_id, const int tag_domain_id )
{
	const auto result { tx.exec(
		"SELECT storage_count, display_count FROM tag_counts WHERE tag_id = $1 AND tag_domain_id = $2",
		pqxx::params { tag_id, tag_domain_id } ) };

	if ( result.empty() ) return { 0, 0 };

	return { result[ 0 ][ 0 ].as< int >(), result[ 0 ][ 1 ].as< int >() };
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

		WHEN( "one character is removed" )
		{
			removeMapping( tx, record, ahri, domain );

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
			removeMapping( tx, record, kindred, domain );
			removeMapping( tx, record, vi, domain );

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

} // namespace idhan::test
