//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "ptr/flatten/DefinitionStore.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

constexpr std::string_view HASH_A { "f69a2836e6ab4e089b9a6695c3d65d2da02f8d69737135e0cec45e173aaafdcd" };
constexpr std::string_view HASH_B { "158408609c78c06ed19884efaef1155fd545b000a907ec9ef9c90fa406b45efc" };

class DefinitionStoreTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-defs-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
};

TEST( PTRDecodeSha256Hex, DecodesLowercase )
{
	const auto decoded = decodeSha256Hex( HASH_A );
	ASSERT_TRUE( decoded.has_value() );
	EXPECT_EQ( std::to_integer< unsigned >( ( *decoded )[ 0 ] ), 0xf6u );
	EXPECT_EQ( std::to_integer< unsigned >( ( *decoded )[ 31 ] ), 0xcdu );
}

TEST( PTRDecodeSha256Hex, DecodesUppercase )
{
	EXPECT_TRUE( decodeSha256Hex( "AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899" ).has_value() );
}

TEST( PTRDecodeSha256Hex, RejectsWrongLength )
{
	EXPECT_FALSE( decodeSha256Hex( "abcd" ).has_value() );
	EXPECT_FALSE( decodeSha256Hex( std::string( 63, 'a' ) ).has_value() );
	EXPECT_FALSE( decodeSha256Hex( std::string( 65, 'a' ) ).has_value() );
	EXPECT_FALSE( decodeSha256Hex( "" ).has_value() );
}

TEST( PTRDecodeSha256Hex, RejectsNonHexCharacters )
{
	EXPECT_FALSE( decodeSha256Hex( std::string( 63, 'a' ) + "z" ).has_value() );
}

TEST_F( DefinitionStoreTest, RoundTripsHashesAtSparseIds )
{
	{
		DefinitionWriter writer { m_dir };
		EXPECT_TRUE( writer.writeHash( 0, HASH_A ) );
		EXPECT_TRUE( writer.writeHash( 194'644'713, HASH_B ) );
		EXPECT_EQ( writer.rejectedHashes(), 0u );
	}

	const DefinitionReader reader { m_dir };

	const auto first = reader.hash( 0 );
	ASSERT_TRUE( first.has_value() );
	ASSERT_EQ( first->size(), SHA256_BYTES );
	const auto expected_first = decodeSha256Hex( HASH_A );
	ASSERT_TRUE( expected_first.has_value() );
	EXPECT_TRUE( std::equal( first->begin(), first->end(), expected_first->begin() ) );

	const auto last = reader.hash( 194'644'713 );
	ASSERT_TRUE( last.has_value() );
	const auto expected_last = decodeSha256Hex( HASH_B );
	ASSERT_TRUE( expected_last.has_value() );
	EXPECT_TRUE( std::equal( last->begin(), last->end(), expected_last->begin() ) );
}

TEST_F( DefinitionStoreTest, UnwrittenHashIdIsAbsent )
{
	{
		DefinitionWriter writer { m_dir };
		writer.writeHash( 10, HASH_A );
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.hash( 9 ).has_value() );
	EXPECT_TRUE( reader.hash( 10 ).has_value() );
	EXPECT_FALSE( reader.hash( 11 ).has_value() );
	EXPECT_FALSE( reader.hash( 4'000'000'000u ).has_value() );
}

TEST_F( DefinitionStoreTest, MalformedHashIsRejectedAndCounted )
{
	{
		DefinitionWriter writer { m_dir };
		EXPECT_FALSE( writer.writeHash( 1, "not-a-hash" ) );
		EXPECT_FALSE( writer.writeHash( 2, std::string( 64, 'z' ) ) );
		EXPECT_EQ( writer.rejectedHashes(), 2u );
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.hash( 1 ).has_value() );
	EXPECT_FALSE( reader.hash( 2 ).has_value() );
}

TEST_F( DefinitionStoreTest, RoundTripsTags )
{
	{
		DefinitionWriter writer { m_dir };
		writer.writeTag( 0, "character:hatsune miku" );
		writer.writeTag( 47'174'184, "creator:someone" );
		writer.writeTag( 5, "solo" );
	}

	const DefinitionReader reader { m_dir };

	const auto first = reader.tag( 0 );
	ASSERT_TRUE( first.has_value() );
	EXPECT_EQ( *first, "character:hatsune miku" );

	const auto last = reader.tag( 47'174'184 );
	ASSERT_TRUE( last.has_value() );
	EXPECT_EQ( *last, "creator:someone" );

	const auto middle = reader.tag( 5 );
	ASSERT_TRUE( middle.has_value() );
	EXPECT_EQ( *middle, "solo" );
}

TEST_F( DefinitionStoreTest, UnwrittenTagIdIsAbsent )
{
	{
		DefinitionWriter writer { m_dir };
		writer.writeTag( 100, "solo" );
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.tag( 99 ).has_value() );
	EXPECT_TRUE( reader.tag( 100 ).has_value() );
	EXPECT_FALSE( reader.tag( 101 ).has_value() );
}

TEST_F( DefinitionStoreTest, LastWriteWinsForARepeatedId )
{
	// PTR definitions are immutable, but a re-sent definition must not corrupt the store.
	{
		DefinitionWriter writer { m_dir };
		writer.writeTag( 3, "first" );
		writer.writeTag( 3, "second" );
	}

	const DefinitionReader reader { m_dir };
	const auto tag = reader.tag( 3 );
	ASSERT_TRUE( tag.has_value() );
	EXPECT_EQ( *tag, "second" );
}

TEST_F( DefinitionStoreTest, EmptyStoreReadsAsAllAbsent )
{
	{
		const DefinitionWriter writer { m_dir };
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.hash( 0 ).has_value() );
	EXPECT_FALSE( reader.tag( 0 ).has_value() );
}

} // namespace
