#include "idhan/SHA256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "idhan/SHA256Hasher.hpp"

namespace idhan::test
{

std::span< const std::byte > bytesOf( const std::string_view text )
{
	return std::as_bytes( std::span { text.data(), text.size() } );
}

TEST_CASE( "SHA-256 matches known digests", "[sha256]" )
{
	CHECK( SHA256::hash( {} ).hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
	CHECK(
		SHA256::hash( bytesOf( "abc" ) ).hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
	CHECK(
		SHA256::hash( bytesOf( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" ) ).hex()
		== "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
	const std::string million_a( 1000000, 'a' );
	CHECK(
		SHA256::hash( bytesOf( million_a ) ).hex()
		== "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" );
}

TEST_CASE( "Incremental SHA-256 is independent of chunk boundaries", "[sha256]" )
{
	std::vector< std::byte > input( 257 );
	for ( std::size_t index {}; index < input.size(); ++index ) input[ index ] = static_cast< std::byte >( index );

	for ( const std::size_t chunk_size : { 1U, 55U, 56U, 63U, 64U, 65U, 128U, 257U } )
	{
		CAPTURE( chunk_size );
		SHA256Hasher hasher;
		hasher.update( {} );
		std::span< const std::byte > remaining { input };
		while ( !remaining.empty() )
		{
			const auto count { std::min( chunk_size, remaining.size() ) };
			hasher.update( remaining.first( count ) );
			hasher.update( {} );
			remaining = remaining.subspan( count );
		}
		CHECK( hasher.finish() == SHA256::hash( input ) );
	}
}

TEST_CASE( "SHA-256 finalization consumes the hasher", "[sha256]" )
{
	SHA256Hasher empty;
	CHECK( empty.finish() == SHA256::hash( {} ) );
	CHECK_THROWS_AS( empty.finish(), std::logic_error );
	CHECK_THROWS_AS( empty.update( {} ), std::logic_error );

	SHA256Hasher hasher;
	hasher.update( bytesOf( "abc" ) );
	CHECK( hasher.finish() == SHA256::hash( bytesOf( "abc" ) ) );
	CHECK_THROWS_AS( hasher.update( bytesOf( "def" ) ), std::logic_error );
	CHECK_THROWS_AS( hasher.finish(), std::logic_error );
}

TEST_CASE( "SHA-256 hex parsing accepts either case and formats lowercase", "[sha256]" )
{
	constexpr std::string_view lower { "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" };
	constexpr std::string_view mixed { "0123456789AbCdEf0123456789aBcDeF0123456789ABCDEF0123456789abcdef" };
	const auto parsed { SHA256::fromHex( mixed ) };
	REQUIRE( parsed );
	CHECK( parsed->hex() == lower );
	CHECK( SHA256::fromHex( parsed->hex() ) == parsed );

	const std::string surrounded { "prefix" + std::string { mixed } + "suffix" };
	CHECK( SHA256::fromHex( std::string_view { surrounded }.substr( 6, 64 ) ) == parsed );
}

TEST_CASE( "SHA-256 rejects malformed hex", "[sha256]" )
{
	for ( const std::size_t length : { 0U, 1U, 32U, 63U, 65U, 128U } )
	{
		CAPTURE( length );
		CHECK_FALSE( SHA256::fromHex( std::string( length, '0' ) ) );
	}
	for ( const char invalid : { 'g', 'G', ' ', '-', '\0', static_cast< char >( 0xff ) } )
	{
		for ( std::size_t position {}; position < 64; ++position )
		{
			CAPTURE( invalid, position );
			std::string text( 64, '0' );
			text[ position ] = invalid;
			const auto parsed { SHA256::fromHex( text ) };
			CHECK_FALSE( parsed );
		}
	}
}

TEST_CASE( "SHA-256 buffers contain exactly 32 bytes", "[sha256]" )
{
	std::array< std::byte, 34 > storage {};
	storage.fill( std::byte { 0xab } );
	const std::span< const std::byte > buffer { storage };
	const auto hash { SHA256::fromBuffer( buffer.subspan( 1, 32 ) ) };
	CHECK( hash.hex() == "abababababababababababababababababababababababababababababababab" );
	CHECK( hash.data().front() == std::byte { 0xab } );
	CHECK( hash.data().back() == std::byte { 0xab } );

	for ( const std::size_t length : { 0U, 1U, 31U, 33U, 34U } )
	{
		CAPTURE( length );
		CHECK_THROWS_AS( SHA256::fromBuffer( buffer.first( length ) ), std::runtime_error );
	}
}

} // namespace idhan::test
