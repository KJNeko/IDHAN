#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

//! Runs createbatchtags over the pairs, returning the tag id per input pair. A pair that resolved to nothing
//! comes back as 0 rather than throwing, so a miss reads as a value mismatch instead of a pqxx error.
static std::vector< int > createBatchTags(
	pqxx::transaction_base& tx,
	const std::vector< std::pair< std::string, std::string > >& pairs )
{
	std::string namespaces {};
	std::string subtags {};

	for ( const auto& [ namespace_text, subtag_text ] : pairs )
	{
		if ( !namespaces.empty() )
		{
			namespaces += ", ";
			subtags += ", ";
		}

		namespaces += tx.quote( namespace_text );
		subtags += tx.quote( subtag_text );
	}

	const auto result { tx.exec(
		"SELECT tag_id FROM createbatchtags(ARRAY[" + namespaces + "]::TEXT[], ARRAY[" + subtags + "]::TEXT[])" ) };

	std::vector< int > ids {};
	ids.reserve( static_cast< std::size_t >( result.size() ) );

	for ( const auto& row : result ) ids.emplace_back( row[ 0 ].is_null() ? 0 : row[ 0 ].as< int >() );

	return ids;
}

SCENARIO_METHOD( MigratedSchema, "Batch tag creation", "[db][tags][batch]" )
{
	pqxx::work tx { connection() };

	GIVEN( "an empty schema" )
	{
		WHEN( "a batch of distinct pairs is created" )
		{
			const auto ids {
				createBatchTags( tx, { { "character", "samus aran" }, { "", "blonde hair" }, { "series", "metroid" } } )
			};

			THEN( "one tag comes back per input pair, in order" )
			{
				REQUIRE( ids.size() == 3 );
				CHECK( ids[ 0 ] > 0 );
				CHECK( ids[ 1 ] > 0 );
				CHECK( ids[ 2 ] > 0 );

				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 3 );
				CHECK(
					tx.query_value< std::string >(
						"SELECT tag_text FROM tags WHERE tag_id = " + std::to_string( ids[ 0 ] ) )
					== "character:samus aran" );
			}
		}

		WHEN( "the same pair appears twice in one batch" )
		{
			const auto ids { createBatchTags( tx, { { "character", "samus aran" }, { "character", "samus aran" } } ) };

			THEN( "both entries name the same tag" )
			{
				REQUIRE( ids.size() == 2 );
				CHECK( ids[ 0 ] > 0 );
				CHECK( ids[ 0 ] == ids[ 1 ] );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 1 );
			}
		}

		WHEN( "the same batch is run twice" )
		{
			const auto first { createBatchTags( tx, { { "character", "samus aran" }, { "series", "metroid" } } ) };
			const auto second { createBatchTags( tx, { { "character", "samus aran" }, { "series", "metroid" } } ) };

			THEN( "the second run returns the tags the first one created" )
			{
				CHECK( first == second );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 2 );
			}
		}
	}

	GIVEN( "a tag that already exists" )
	{
		const auto existing_id { insertTag( tx, "Character", "Samus Aran" ) };

		WHEN( "a batch names it in a different case" )
		{
			const auto ids { createBatchTags( tx, { { "CHARACTER", "SAMUS ARAN" } } ) };

			THEN( "it folds onto the existing tag" )
			{
				REQUIRE( ids.size() == 1 );
				CHECK( ids[ 0 ] == existing_id );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 1 );
			}
		}
	}
}

} // namespace idhan::test
