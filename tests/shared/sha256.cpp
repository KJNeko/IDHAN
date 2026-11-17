#include "idhan/SHA256.hpp"

#include <gtest/gtest.h>

#include <span>
#include <string_view>

namespace idhan::tests
{

TEST( SHA256, HashesKnownInputs )
{
	EXPECT_EQ( SHA256::hash( {} ).hex(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );

	constexpr std::string_view input { "abc" };
	const auto digest { SHA256::hash( std::as_bytes( std::span { input.data(), input.size() } ) ) };
	EXPECT_EQ( digest.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
}

TEST( SHA256, ParsesHexAndPreservesBytes )
{
	const auto digest_e { SHA256::fromHex( "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD" ) };
	ASSERT_TRUE( digest_e.has_value() ) << digest_e.error();
	const auto& digest { digest_e.value() };
	EXPECT_EQ( digest.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
	EXPECT_EQ( SHA256::fromBuffer( digest.data() ), digest );
	EXPECT_EQ( digest.data().front(), std::byte { 0xba } );
	EXPECT_EQ( digest.data().back(), std::byte { 0xad } );
}

TEST( SHA256, RejectsInvalidInputs )
{
	EXPECT_FALSE( SHA256::fromHex( "abc" ).has_value() );
	EXPECT_FALSE( SHA256::fromHex( std::string( 64, 'g' ) ).has_value() );
	EXPECT_THROW( SHA256::fromBuffer( {} ), std::runtime_error );
}

} // namespace idhan::tests
