#include "httpapi/ImportBody.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <folly/FileUtil.h>
#include <folly/coro/BlockingWait.h>

#include <algorithm>
#include <string>

#include "idhan/http/UploadPipeline.hpp"

namespace idhan::test
{

TEST_CASE( "Imports retain complete bodies across the upload spill boundary", "[import][upload]" )
{
	const auto size { GENERATE(
		std::size_t { 0 },
		http::request_memory_limit - 1,
		http::request_memory_limit,
		http::request_memory_limit + 1,
		3 * http::request_memory_limit + 17 ) };
	std::string content( size, '\0' );
	for ( std::size_t index {}; index < size; ++index ) content[ index ] = static_cast< char >( index % 251 );
	const auto expected_bytes { std::as_bytes( std::span { content.data(), content.size() } ) };
	const auto expected_hash { SHA256::hash( expected_bytes.data(), expected_bytes.size() ) };

	http::UploadPipeline pipeline { std::filesystem::temp_directory_path() };
	for ( std::size_t offset {}; offset < size; )
	{
		const auto count { std::min( http::file_chunk_bytes, size - offset ) };
		folly::coro::blockingWait( pipeline.append( folly::IOBuf::copyBuffer( content.data() + offset, count ) ) );
		offset += count;
	}
	auto request_body { folly::coro::blockingWait( pipeline.finish() ) };
	REQUIRE( std::holds_alternative< http::TemporaryUpload >( request_body ) == ( size > http::request_memory_limit ) );
	auto input { httpapi::ImportBody::create( std::move( request_body ) ) };
	REQUIRE( input.has_value() );
	// The upload descriptor has been released; moving the owner must preserve the mapping.
	auto moved { std::move( *input ) };
	CHECK( std::ranges::equal( moved.bytes(), expected_bytes ) );
	CHECK( moved.hash() == expected_hash );

	auto stored { folly::File::temporary() };
	REQUIRE(
		folly::writeFull( stored.fd(), moved.bytes().data(), moved.bytes().size() ) == static_cast< ssize_t >( size ) );
	std::string downloaded( size, '\0' );
	REQUIRE( folly::preadFull( stored.fd(), downloaded.data(), size, 0 ) == static_cast< ssize_t >( size ) );
	CHECK( downloaded == content );
}

TEST_CASE( "Imports reject incomplete temporary files", "[import][upload]" )
{
	http::TemporaryUpload upload { folly::File::temporary(), 9, SHA256 {}, {} };
	const auto input { httpapi::ImportBody::create( http::RequestBody { std::move( upload ) } ) };
	REQUIRE_FALSE( input.has_value() );
	CHECK( input.error() == "Upload file size does not match received bytes" );
}

TEST_CASE( "Imports report unusable temporary descriptors", "[import][upload]" )
{
	http::TemporaryUpload upload { folly::File {}, 9, SHA256 {}, {} };
	const auto input { httpapi::ImportBody::create( http::RequestBody { std::move( upload ) } ) };
	REQUIRE_FALSE( input.has_value() );
}

} // namespace idhan::test
