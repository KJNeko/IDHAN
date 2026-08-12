#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

#include "ptr/flatten/ChunkFormat.hpp"

using namespace idhan::hydrus::ptr;

std::array< std::byte, SHA256_BYTES > sha( const unsigned char fill )
{
	std::array< std::byte, SHA256_BYTES > out {};
	out.fill( static_cast< std::byte >( fill ) );
	return out;
}

//! A lookup over a fixed table, so the writer can be tested with no DefinitionStore on disk.
static TagLookup lookupOver( const std::map< std::uint32_t, std::string >& table )
{
	return [ &table ]( const std::uint32_t tag_id ) -> std::optional< std::string_view >
	{
		const auto it = table.find( tag_id );
		if ( it == table.end() ) return std::nullopt;
		return std::string_view( it->second );
	};
}

class ChunkFormatTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-chunk-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
		m_path = m_dir / "chunk-00000.idhanptr";
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
	std::filesystem::path m_path;
};

TEST( PTRChunkHeader, IsTwentyEightBytes )
{
	EXPECT_EQ( sizeof( ChunkHeader ), 28u );
}

TEST_F( ChunkFormatTest, RoundTripsOneRecord )
{
	const std::map< std::uint32_t, std::string > tags { { 10, "solo" }, { 20, "character:miku" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 0xAB ), { 10, 20 }, {} );
		EXPECT_EQ( writer.recordCount(), 1u );

		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.records, 1u );
		EXPECT_EQ( stats.mappings, 2u );
		EXPECT_EQ( stats.missing_definitions, 0u );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.records.size(), 1u );
	ASSERT_EQ( chunk.strings.size(), 2u );

	EXPECT_EQ( chunk.records[ 0 ].sha256, sha( 0xAB ) );
	ASSERT_EQ( chunk.records[ 0 ].add_indices.size(), 2u );
	EXPECT_TRUE( chunk.records[ 0 ].del_indices.empty() );

	// The string table is sorted by ptr_tag_id, so index 0 is tag 10.
	EXPECT_EQ( chunk.strings[ 0 ].ptr_tag_id, 10u );
	EXPECT_EQ( chunk.strings[ 0 ].tag, "solo" );
	EXPECT_EQ( chunk.strings[ 1 ].ptr_tag_id, 20u );
	EXPECT_EQ( chunk.strings[ 1 ].tag, "character:miku" );

	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 0 ] ].tag, "solo" );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 1 ] ].tag, "character:miku" );
}

TEST_F( ChunkFormatTest, StringTableIsSortedByPtrTagIdRegardlessOfInsertionOrder )
{
	const std::map< std::uint32_t, std::string > tags { { 5, "e" }, { 1, "a" }, { 9, "i" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 1 ), { 9, 5 }, { 1 } );
		writer.finish( lookupOver( tags ) );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.strings.size(), 3u );
	EXPECT_EQ( chunk.strings[ 0 ].ptr_tag_id, 1u );
	EXPECT_EQ( chunk.strings[ 1 ].ptr_tag_id, 5u );
	EXPECT_EQ( chunk.strings[ 2 ].ptr_tag_id, 9u );

	ASSERT_EQ( chunk.records[ 0 ].add_indices.size(), 2u );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 0 ] ].tag, "i" );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 1 ] ].tag, "e" );
	ASSERT_EQ( chunk.records[ 0 ].del_indices.size(), 1u );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].del_indices[ 0 ] ].tag, "a" );
}

TEST_F( ChunkFormatTest, TagsAreInternedOnceAcrossRecords )
{
	const std::map< std::uint32_t, std::string > tags { { 7, "shared" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 1 ), { 7 }, {} );
		writer.addRecord( sha( 2 ), { 7 }, {} );
		writer.addRecord( sha( 3 ), {}, { 7 } );
		writer.finish( lookupOver( tags ) );
	}

	const auto chunk = readChunk( m_path );
	EXPECT_EQ( chunk.strings.size(), 1u );
	EXPECT_EQ( chunk.records.size(), 3u );
}

TEST_F( ChunkFormatTest, MissingDefinitionsAreDroppedAndCounted )
{
	const std::map< std::uint32_t, std::string > tags { { 1, "known" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 1 ), { 1, 999 }, { 998 } );
		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.mappings, 1u );
		EXPECT_EQ( stats.missing_definitions, 2u );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.records.size(), 1u );
	ASSERT_EQ( chunk.strings.size(), 1u );
	EXPECT_EQ( chunk.records[ 0 ].add_indices.size(), 1u );
	EXPECT_TRUE( chunk.records[ 0 ].del_indices.empty() );
}

TEST_F( ChunkFormatTest, RecordWithNoResolvableTagsIsStillWritten )
{
	// The record hash is real PTR data even when every tag on it is undefined; dropping the
	// record would silently lose a file the corpus knows about.
	const std::map< std::uint32_t, std::string > tags {};

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 4 ), { 42 }, {} );
		writer.finish( lookupOver( tags ) );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.records.size(), 1u );
	EXPECT_TRUE( chunk.records[ 0 ].add_indices.empty() );
	EXPECT_TRUE( chunk.strings.empty() );
}

TEST_F( ChunkFormatTest, RoundTripsAnEmptyChunk )
{
	const std::map< std::uint32_t, std::string > tags {};

	{
		ChunkWriter writer { m_path };
		EXPECT_TRUE( writer.empty() );
		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.records, 0u );
	}

	const auto chunk = readChunk( m_path );
	EXPECT_TRUE( chunk.records.empty() );
	EXPECT_TRUE( chunk.strings.empty() );
}

TEST_F( ChunkFormatTest, RejectsAFileWithTheWrongMagic )
{
	{
		std::ofstream file { m_path, std::ios::binary };
		file << "NOTACHUNKNOTACHUNKNOTACHUNK!";
	}

	EXPECT_THROW( ( void ) readChunk( m_path ), std::runtime_error );
}

TEST_F( ChunkFormatTest, RoundTripsManyRecords )
{
	std::map< std::uint32_t, std::string > tags;
	for ( std::uint32_t i = 0; i < 200; ++i ) tags[ i ] = "tag:" + std::to_string( i );

	{
		ChunkWriter writer { m_path };
		for ( std::uint32_t r = 0; r < 1000; ++r )
			writer.addRecord( sha( static_cast< unsigned char >( r % 256 ) ), { r % 200, ( r + 1 ) % 200 }, {} );
		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.records, 1000u );
		EXPECT_EQ( stats.mappings, 2000u );
	}

	const auto chunk = readChunk( m_path );
	EXPECT_EQ( chunk.records.size(), 1000u );
	EXPECT_EQ( chunk.strings.size(), 200u );
	for ( const auto& record : chunk.records ) EXPECT_EQ( record.add_indices.size(), 2u );
}

