#include <string>
#include <utility>
#include <vector>

#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

namespace
{

//! Runs createbatchtags over the pairs, returning the tag id per input pair. A pair that resolved to nothing
//! comes back as 0 rather than throwing, so a miss reads as a value mismatch instead of a pqxx error.
std::vector< int > createBatchTags(
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

} // namespace

TEST_F( MigratedSchema, BatchReturnsOneTagPerInputPairInOrder )
{
	pqxx::work tx { connection() };

	const auto ids {
		createBatchTags( tx, { { "character", "samus aran" }, { "", "blonde hair" }, { "series", "metroid" } } )
	};

	ASSERT_EQ( ids.size(), 3 );
	EXPECT_GT( ids[ 0 ], 0 );
	EXPECT_GT( ids[ 1 ], 0 );
	EXPECT_GT( ids[ 2 ], 0 );

	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 3 );
	EXPECT_EQ(
		tx.query_value< std::string >( "SELECT tag_text FROM tags WHERE tag_id = " + std::to_string( ids[ 0 ] ) ),
		"character:samus aran" );
}

TEST_F( MigratedSchema, BatchFoldsMixedCaseOntoTheTagThatAlreadyExists )
{
	pqxx::work tx { connection() };

	const auto existing_id { insertTag( tx, "Character", "Samus Aran" ) };

	const auto ids { createBatchTags( tx, { { "CHARACTER", "SAMUS ARAN" } } ) };

	ASSERT_EQ( ids.size(), 1 );
	EXPECT_EQ( ids[ 0 ], existing_id );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 1 );
}

TEST_F( MigratedSchema, BatchGivesRepeatedPairsTheSameTag )
{
	pqxx::work tx { connection() };

	const auto ids { createBatchTags( tx, { { "character", "samus aran" }, { "character", "samus aran" } } ) };

	ASSERT_EQ( ids.size(), 2 );
	EXPECT_GT( ids[ 0 ], 0 );
	EXPECT_EQ( ids[ 0 ], ids[ 1 ] );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 1 );
}

TEST_F( MigratedSchema, BatchRunTwiceReturnsTheSameTags )
{
	pqxx::work tx { connection() };

	const auto first { createBatchTags( tx, { { "character", "samus aran" }, { "series", "metroid" } } ) };
	const auto second { createBatchTags( tx, { { "character", "samus aran" }, { "series", "metroid" } } ) };

	EXPECT_EQ( first, second );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 2 );
}

} // namespace idhan::test
