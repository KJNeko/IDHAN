#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace idhan::test
{

struct CurlError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

struct CurlResponse
{
	long status { 0 };
	std::uint64_t uploaded_bytes { 0 };
	std::vector< unsigned > informational_statuses {};
	std::string body {};
	std::vector< std::pair< std::string, std::string > > headers {};

	//! Returns the value of \p name, matched case-insensitively, or nullopt when the response carried no such header.
	[[nodiscard]] std::optional< std::string_view > header( std::string_view name ) const;

	[[nodiscard]] bool hasHeader( const std::string_view name ) const { return header( name ).has_value(); }

	//! Returns every value sent for \p name, in the order they arrived.
	[[nodiscard]] std::vector< std::string_view > headerValues( std::string_view name ) const;
};

struct CurlRequest
{
	std::string url {};
	std::string method { "GET" };
	//! Full header lines ( "Name: value" ). A line of "Name:" suppresses a header curl would otherwise add itself.
	std::vector< std::string > headers {};
	std::string body {};
	std::chrono::milliseconds timeout { std::chrono::seconds( 10 ) };
	bool follow_redirects { false };
	std::chrono::milliseconds expect_continue_timeout { std::chrono::seconds( 1 ) };
};

//! Performs \p request and returns the status, body, and response headers.
//! \throws CurlError if the transfer never completed ( connection refused, timeout, malformed URL ).
[[nodiscard]] CurlResponse send( const CurlRequest& request );

[[nodiscard]] CurlResponse get( std::string url, std::vector< std::string > headers = {} );

[[nodiscard]] CurlResponse head( std::string url, std::vector< std::string > headers = {} );

[[nodiscard]] CurlResponse deleteRequest( std::string url, std::vector< std::string > headers = {} );

[[nodiscard]] CurlResponse post(
	std::string url,
	std::string body,
	std::string content_type = "application/octet-stream",
	std::vector< std::string > headers = {} );

[[nodiscard]] CurlResponse put(
	std::string url,
	std::string body,
	std::string content_type = "application/octet-stream",
	std::vector< std::string > headers = {} );

[[nodiscard]] CurlResponse postJson( std::string url, std::string json, std::vector< std::string > headers = {} );

[[nodiscard]] CurlResponse putJson( std::string url, std::string json, std::vector< std::string > headers = {} );

//! Sends \p bytes as the body of \p method, without any text conversion.
[[nodiscard]] CurlResponse sendBytes(
	std::string url,
	std::string method,
	std::span< const std::byte > bytes,
	std::string content_type = "application/octet-stream",
	std::vector< std::string > headers = {} );

//! Sends the whole of \p path as the body of \p method.
//! \throws CurlError if the file cannot be read.
[[nodiscard]] CurlResponse sendFile(
	std::string url,
	std::string method,
	const std::filesystem::path& path,
	std::string content_type = "application/octet-stream",
	std::vector< std::string > headers = {} );

//! GETs the closed range [ \p first, \p last ], or the open-ended range from \p first when \p last is nullopt.
[[nodiscard]] CurlResponse getRange(
	std::string url,
	std::uint64_t first,
	std::optional< std::uint64_t > last = std::nullopt,
	std::vector< std::string > headers = {} );

//! GETs the final \p length bytes ( "Range: bytes=-length" ).
[[nodiscard]] CurlResponse getSuffixRange(
	std::string url,
	std::uint64_t length,
	std::vector< std::string > headers = {} );

//! Builds a "Range: bytes=..." header line for a closed or open-ended range.
[[nodiscard]] std::string rangeHeader( std::uint64_t first, std::optional< std::uint64_t > last = std::nullopt );

//! Builds a "Range: bytes=-length" header line.
[[nodiscard]] std::string suffixRangeHeader( std::uint64_t length );

[[nodiscard]] std::string urlEncode( std::string_view value );

//! Appends \p params to \p url as a percent-encoded query string.
[[nodiscard]] std::string urlWithQuery(
	std::string_view url,
	const std::vector< std::pair< std::string, std::string > >& params );

//! Builds "http://127.0.0.1:<port><path>", with \p path taken as an absolute path.
[[nodiscard]] std::string localUrl( std::uint16_t port, std::string_view path );

} // namespace idhan::test
