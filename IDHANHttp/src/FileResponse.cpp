#include "idhan/http/FileResponse.hpp"

#include <proxygen/lib/utils/HTTPTime.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <format>

#include "idhan/http/FileBodySource.hpp"

namespace idhan::http
{

namespace
{

constexpr std::chrono::seconds default_cache_max_age { std::chrono::hours { 1 } };
constexpr std::chrono::seconds immutable_cache_max_age { std::chrono::hours { 24 * 365 } };

[[nodiscard]] std::string quoteEtag( const std::string_view etag )
{
	return std::format( "\"{}\"", etag );
}

[[nodiscard]] std::string formatHttpDate( const std::chrono::system_clock::time_point time )
{
	const auto seconds { std::chrono::system_clock::to_time_t( time ) };
	std::tm tm {};
	gmtime_r( &seconds, &tm );
	char buffer[ 64 ];
	const auto length { std::strftime( buffer, sizeof( buffer ), "%a, %d %b %Y %H:%M:%S GMT", &tm ) };
	return std::string { buffer, length };
}

// Comma-separated list of quoted (or weak) entity tags, or "*".
[[nodiscard]] bool etagListMatches( std::string_view header, const std::string_view quoted_etag )
{
	if ( header.empty() ) return false;
	if ( header == "*" ) return true;

	std::size_t start { 0 };
	while ( start < header.size() )
	{
		auto end { header.find( ',', start ) };
		if ( end == std::string_view::npos ) end = header.size();

		auto candidate { header.substr( start, end - start ) };
		while ( !candidate.empty() && candidate.front() == ' ' ) candidate.remove_prefix( 1 );
		while ( !candidate.empty() && candidate.back() == ' ' ) candidate.remove_suffix( 1 );
		if ( candidate.starts_with( "W/" ) ) candidate.remove_prefix( 2 );

		if ( candidate == quoted_etag ) return true;

		start = end + 1;
	}
	return false;
}

enum class RangeOutcome
{
	none,
	satisfiable,
	unsatisfiable
};

struct RangeResult
{
	RangeOutcome outcome { RangeOutcome::none };
	std::uint64_t begin { 0 };
	std::uint64_t end { 0 };
};

constexpr std::string_view bytes_prefix { "bytes=" };

// Single-range only: "bytes=N-" or "bytes=N-M". Suffix ranges ("bytes=-N") and
// multi-range requests are treated as absent, per the "malformed falls back to
// a full 200" policy in docs/designs/http.md.
[[nodiscard]] RangeResult parseRange( const std::string_view header, const std::uint64_t size )
{
	if ( !header.starts_with( bytes_prefix ) ) return {};

	const auto spec { header.substr( bytes_prefix.size() ) };
	if ( spec.contains( ',' ) ) return {};

	const auto dash { spec.find( '-' ) };
	if ( dash == std::string_view::npos || dash == 0 ) return {};

	std::uint64_t begin { 0 };
	const auto begin_text { spec.substr( 0, dash ) };
	if ( std::from_chars( begin_text.data(), begin_text.data() + begin_text.size(), begin ).ec != std::errc {} )
		return {};

	const auto end_text { spec.substr( dash + 1 ) };
	std::uint64_t end { size == 0 ? 0 : size - 1 };
	if ( !end_text.empty() )
	{
		if ( std::from_chars( end_text.data(), end_text.data() + end_text.size(), end ).ec != std::errc {} ) return {};
	}

	if ( size == 0 || begin >= size ) return { RangeOutcome::unsatisfiable, 0, 0 };

	if ( begin > end ) return {};

	end = std::min( end, size - 1 );

	return { RangeOutcome::satisfiable, begin, end };
}

} // namespace

HTTPTask< Response > FileResponse::create( const RequestContext& context, folly::File file, FileMetadata metadata )
{
	const auto& message { context.message() };
	const auto& headers { message.getHeaders() };
	const bool head { message.getMethod() == proxygen::HTTPMethod::HEAD };
	const auto quoted_etag { quoteEtag( metadata.etag ) };
	const auto last_modified { formatHttpDate( metadata.modified ) };

	Response response;
	response.message.setHTTPVersion( message.getHTTPVersion().first, message.getHTTPVersion().second );

	auto& response_headers { response.message.getHeaders() };
	response_headers.set( "Server", "IDHAN" );
	response_headers.set( "ETag", quoted_etag );
	response_headers.set( "Last-Modified", last_modified );
	response_headers.set( "Accept-Ranges", "bytes" );
	response_headers.set(
		"Cache-Control",
		std::format(
			"private, {}max-age={}",
			metadata.immutable ? "immutable, " : "",
			( metadata.immutable ? immutable_cache_max_age : default_cache_max_age ).count() ) );
	if ( !metadata.filename.empty() )
		response_headers.set(
			"Content-Disposition",
			std::format(
				"{}; filename=\"{}\"", metadata.attachment ? "attachment" : "inline", metadata.filename ) );

	const auto setStatus = [ &response ]( const std::uint16_t status )
	{
		response.message.setStatusCode( status );
		response.message.setStatusMessage( proxygen::HTTPMessage::getDefaultReason( status ) );
	};

	const auto& if_match { headers.getSingleOrEmpty( "If-Match" ) };
	if ( !if_match.empty() && !etagListMatches( if_match, quoted_etag ) )
	{
		setStatus( 412 );
		response.body = std::monostate {};
		co_return response;
	}

	const auto& if_none_match { headers.getSingleOrEmpty( "If-None-Match" ) };
	bool not_modified { false };
	if ( !if_none_match.empty() )
	{
		not_modified = etagListMatches( if_none_match, quoted_etag );
	}
	else
	{
		const auto& if_modified_since { headers.getSingleOrEmpty( "If-Modified-Since" ) };
		if ( !if_modified_since.empty() )
		{
			const auto parsed { proxygen::parseHTTPDateTime( if_modified_since ) };
			if ( parsed
			     && *parsed
			            >= std::chrono::duration_cast< std::chrono::seconds >( metadata.modified.time_since_epoch() )
			                   .count() )
				not_modified = true;
		}
	}

	if ( not_modified )
	{
		setStatus( 304 );
		response.body = std::monostate {};
		co_return response;
	}

	const auto& if_unmodified_since { headers.getSingleOrEmpty( "If-Unmodified-Since" ) };
	if ( if_match.empty() && !if_unmodified_since.empty() )
	{
		const auto parsed { proxygen::parseHTTPDateTime( if_unmodified_since ) };
		if ( parsed
		     && *parsed < std::chrono::duration_cast< std::chrono::seconds >( metadata.modified.time_since_epoch() )
		                      .count() )
		{
			setStatus( 412 );
			response.body = std::monostate {};
			co_return response;
		}
	}

	std::uint64_t begin { 0 };
	std::uint64_t end { metadata.size == 0 ? 0 : metadata.size - 1 };
	bool ranged { false };

	const auto& range_header { headers.getSingleOrEmpty( "Range" ) };
	if ( !range_header.empty() )
	{
		const auto& if_range { headers.getSingleOrEmpty( "If-Range" ) };
		const bool range_applies { if_range.empty() || if_range == quoted_etag };

		if ( range_applies )
		{
			const auto range { parseRange( range_header, metadata.size ) };
			if ( range.outcome == RangeOutcome::unsatisfiable )
			{
				setStatus( 416 );
				response_headers.set( "Content-Range", std::format( "bytes */{}", metadata.size ) );
				response.body = std::monostate {};
				co_return response;
			}
			if ( range.outcome == RangeOutcome::satisfiable )
			{
				begin = range.begin;
				end = range.end;
				ranged = true;
			}
		}
	}

	const std::uint64_t length { metadata.size == 0 ? 0 : end - begin + 1 };

	setStatus( ranged ? 206 : 200 );
	response_headers.set( "Content-Type", metadata.content_type );
	response_headers.set( "Content-Length", std::to_string( length ) );
	if ( ranged ) response_headers.set( "Content-Range", std::format( "bytes {}-{}/{}", begin, end, metadata.size ) );

	if ( head )
	{
		response.body = std::monostate {};
		co_return response;
	}

	auto* const source { new FileBodySource { response.message, std::move( file ), begin, length } };
	source->setHeapAllocated();
	response.body = proxygen::coro::HTTPSourceHolder { source };

	co_return response;
}

} // namespace idhan::http
