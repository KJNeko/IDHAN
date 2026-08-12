#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/HexEncode.hpp"

using namespace idhan::hydrus::ptr;

TEST( PTRHexEncode, EncodesAllZeroes )
{
	const std::array< std::byte, SHA256_BYTES > bytes {};
	EXPECT_EQ( toHex( bytes ), std::string( 64, '0' ) );
}

TEST( PTRHexEncode, EncodesLowercaseWithLeadingZeroNibbles )
{
	const std::array< std::byte, 4 > bytes {
		std::byte { 0x0A }, std::byte { 0xB0 }, std::byte { 0xFF }, std::byte { 0x00 }
	};
	EXPECT_EQ( toHex( bytes ), "0ab0ff00" );
}

TEST( PTRHexEncode, RoundTripsThroughDecodeSha256Hex )
{
	constexpr std::string_view original { "f69a2836e6ab4e089b9a6695c3d65d2da02f8d69737135e0cec45e173aaafdcd" };
	const auto decoded = decodeSha256Hex( original );
	ASSERT_TRUE( decoded.has_value() );
	EXPECT_EQ( toHex( *decoded ), original );
}

TEST( PTRHexEncode, EmptyInputEncodesEmpty )
{
	EXPECT_TRUE( toHex( {} ).empty() );
}

