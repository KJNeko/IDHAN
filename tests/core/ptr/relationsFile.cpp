//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

#include "ptr/flatten/RelationsFile.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

RelationEvent rel( const std::uint32_t a, const std::uint32_t b, const std::uint16_t index, const EventOp op )
{
	return RelationEvent { a, b, index, static_cast< std::uint8_t >( op ), 0 };
}

TagLookup lookupOver( const std::map< std::uint32_t, std::string >& table )
{
	return [ &table ]( const std::uint32_t tag_id ) -> std::optional< std::string_view >
	{
		const auto it = table.find( tag_id );
		if ( it == table.end() ) return std::nullopt;
		return std::string_view( it->second );
	};
}

TEST( PTRCollapseRelations, EmptyInputProducesNothing )
{
	EXPECT_TRUE( collapseRelations( {} ).empty() );
}

TEST( PTRCollapseRelations, LoneAddSurvives )
{
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ) } );
	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].a, 1u );
	EXPECT_EQ( collapsed[ 0 ].b, 2u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Add );
}

TEST( PTRCollapseRelations, AddDeleteAddCollapsesToOneAdd )
{
	const auto collapsed = collapseRelations(
		{ rel( 1, 2, 0, EventOp::Add ), rel( 1, 2, 1, EventOp::Delete ), rel( 1, 2, 2, EventOp::Add ) } );

	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Add );
}

TEST( PTRCollapseRelations, AddDeleteCollapsesToOneDelete )
{
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ), rel( 1, 2, 4, EventOp::Delete ) } );

	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Delete );
}

TEST( PTRCollapseRelations, DistinctPairsAreIndependent )
{
	const auto collapsed = collapseRelations(
		{ rel( 1, 2, 0, EventOp::Add ), rel( 3, 4, 0, EventOp::Add ), rel( 1, 2, 1, EventOp::Delete ) } );

	ASSERT_EQ( collapsed.size(), 2u );
	// Output is sorted by (a, b), so (1,2) comes first.
	EXPECT_EQ( collapsed[ 0 ].a, 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Delete );
	EXPECT_EQ( collapsed[ 1 ].a, 3u );
	EXPECT_EQ( collapsed[ 1 ].op, EventOp::Add );
}

TEST( PTRCollapseRelations, DirectionMatters )
{
	// (1,2) and (2,1) are different relationships and must not collapse together.
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ), rel( 2, 1, 0, EventOp::Add ) } );
	EXPECT_EQ( collapsed.size(), 2u );
}

TEST( PTRCollapseRelations, UnorderedInputIsHandled )
{
	const auto collapsed = collapseRelations(
		{ rel( 1, 2, 5, EventOp::Delete ), rel( 1, 2, 1, EventOp::Add ), rel( 1, 2, 3, EventOp::Add ) } );

	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Delete );
}

class RelationsFileTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-relations-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
		m_path = m_dir / RELATIONS_FILENAME;
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
	std::filesystem::path m_path;
};

TEST_F( RelationsFileTest, RoundTripsParentsAndSiblings )
{
	const std::map< std::uint32_t, std::string > tags { { 1, "bad" }, { 2, "good" }, { 3, "child" }, { 4, "parent" } };

	const std::vector< CollapsedRelation > siblings { { 1, 2, EventOp::Add } };
	const std::vector< CollapsedRelation > parents { { 3, 4, EventOp::Delete } };

	const auto stats = writeRelationsFile( m_path, parents, siblings, lookupOver( tags ) );
	EXPECT_EQ( stats.parents, 1u );
	EXPECT_EQ( stats.siblings, 1u );
	EXPECT_EQ( stats.missing_definitions, 0u );

	const auto file = readRelationsFile( m_path );

	ASSERT_EQ( file.siblings.size(), 1u );
	EXPECT_EQ( file.strings[ file.siblings[ 0 ].a_index ].tag, "bad" );
	EXPECT_EQ( file.strings[ file.siblings[ 0 ].b_index ].tag, "good" );
	EXPECT_EQ( file.siblings[ 0 ].op, EventOp::Add );

	ASSERT_EQ( file.parents.size(), 1u );
	EXPECT_EQ( file.strings[ file.parents[ 0 ].a_index ].tag, "child" );
	EXPECT_EQ( file.strings[ file.parents[ 0 ].b_index ].tag, "parent" );
	EXPECT_EQ( file.parents[ 0 ].op, EventOp::Delete );
}

TEST_F( RelationsFileTest, PairWithAnUndefinedTagIsDroppedAndCounted )
{
	const std::map< std::uint32_t, std::string > tags { { 1, "known" } };
	const std::vector< CollapsedRelation > siblings { { 1, 999, EventOp::Add } };

	const auto stats = writeRelationsFile( m_path, {}, siblings, lookupOver( tags ) );
	EXPECT_EQ( stats.siblings, 0u );
	EXPECT_EQ( stats.missing_definitions, 1u );

	EXPECT_TRUE( readRelationsFile( m_path ).siblings.empty() );
}

TEST_F( RelationsFileTest, RoundTripsAnEmptyFile )
{
	const std::map< std::uint32_t, std::string > tags {};
	writeRelationsFile( m_path, {}, {}, lookupOver( tags ) );

	const auto file = readRelationsFile( m_path );
	EXPECT_TRUE( file.parents.empty() );
	EXPECT_TRUE( file.siblings.empty() );
	EXPECT_TRUE( file.strings.empty() );
}

TEST_F( RelationsFileTest, RejectsAChunkFileAsRelations )
{
	// The two formats share a header shape; only the magic distinguishes them.
	const std::map< std::uint32_t, std::string > tags { { 1, "a" } };
	{
		ChunkWriter writer { m_path };
		writer.addRecord( std::array< std::byte, SHA256_BYTES > {}, { 1 }, {} );
		writer.finish( lookupOver( tags ) );
	}

	EXPECT_THROW( ( void ) readRelationsFile( m_path ), std::runtime_error );
}

} // namespace
