#include "info.hpp"

#include <folly/io/IOBuf.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apiAuth.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/response/FileResponse.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

static http::HTTPTask< http::Response > serveStaticHtml(
	http::RequestContext context,
	std::string filename,
	std::string missing_message )
{
	const auto file_path { getStaticPath() / filename };

	auto file_e { folly::File::makeFile( file_path.c_str(), O_RDONLY ) };
	if ( !file_e ) co_return http::Response::error( 404, missing_message );

	auto file { std::move( *file_e ) };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 || !S_ISREG( status.st_mode ) )
		co_return http::Response::error( 404, missing_message );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = std::format( "{:x}-{:x}", status.st_mtime, status.st_size ),
		.content_type = "text/html; charset=utf-8",
		.filename = filename,
	};

	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
}

http::HTTPTask< http::EncodedResult< std::string, "text/html" > > apiDocsIndex( http::RequestContext context )
{
	co_return http::encodedResponse< std::string, "text/html" >(
		co_await serveStaticHtml( std::move( context ), "apidocs.html", "API docs are not built into this server" ) );
}

http::HTTPTask< http::EncodedResult< std::string, "text/html" > > logPretty( http::RequestContext context )
{
	co_return http::encodedResponse< std::string, "text/html" >( co_await serveStaticHtml(
		std::move( context ), "logs.html", "The log viewer is not installed on this server" ) );
}

[[nodiscard]] static spdlog::level::level_enum parseLevelString( const std::string_view level_str )
{
	if ( level_str == "trace" ) return spdlog::level::trace;
	if ( level_str == "debug" ) return spdlog::level::debug;
	if ( level_str == "info" ) return spdlog::level::info;
	if ( level_str == "warning" || level_str == "warn" ) return spdlog::level::warn;
	if ( level_str == "error" || level_str == "err" ) return spdlog::level::err;
	if ( level_str == "critical" ) return spdlog::level::critical;
	return spdlog::level::info;
}

static LogEntryResponse logEntryResponse( const spdlog::details::log_msg_buffer& entry )
{
	const auto micros {
		std::chrono::duration_cast< std::chrono::microseconds >( entry.time.time_since_epoch() ).count()
	};
	const auto level { spdlog::level::to_string_view( entry.level ) };
	LogEntryResponse response {
		static_cast< std::uint64_t >( micros ),
		std::string { level.data(), level.size() },
		static_cast< std::uint64_t >( entry.thread_id ),
		std::string { entry.payload.data(), entry.payload.size() }
	};
	if ( response.message.starts_with( log::ready_marker ) ) response.marker.emplace( "ready" );

	return response;
}

static LogResponse renderLogJson(
	const std::vector< spdlog::details::log_msg_buffer >& entries,
	const spdlog::level::level_enum min_level,
	const std::optional< std::uint64_t > since_seconds,
	const std::optional< std::uint64_t > after_micros,
	const std::size_t limit )
{
	std::vector< const spdlog::details::log_msg_buffer* > selected {};
	std::optional< std::uint64_t > ready_at {};

	for ( const auto& entry : entries )
	{
		const auto micros { static_cast< std::uint64_t >(
			std::chrono::duration_cast< std::chrono::microseconds >( entry.time.time_since_epoch() ).count() ) };

		if ( std::string_view { entry.payload.data(), entry.payload.size() }.starts_with( log::ready_marker ) )
			ready_at = micros;

		if ( entry.level < min_level ) continue;

		if ( since_seconds.has_value() && micros / 1000000 < *since_seconds ) continue;

		if ( after_micros.has_value() && micros <= *after_micros ) continue;

		selected.push_back( &entry );
	}

	const auto first { selected.size() > limit ? selected.size() - limit : std::size_t { 0 } };

	LogEntriesResponse response { {}, first != 0, ready_at };
	response.entries.reserve( selected.size() - first );
	for ( std::size_t index { first }; index < selected.size(); ++index )
		response.entries.push_back( logEntryResponse( *selected[ index ] ) );

	return http::JsonResponse< LogEntriesResponse > { std::move( response ) };
}

static http::HTTPTask< LogResponse > fetchLogImpl( const FetchLogQuery& parameters, const bool json_by_default )
{
	const std::string format { parameters.format.value_or( json_by_default ? "json" : "text" ) };
	if ( format != "text" && format != "json" ) co_return http::Response::error( 400, "format must be text or json" );

	auto logger { log::getServerLogger() };
	if ( !logger ) co_return http::Response::error( 500, "Could not get default spdlog logger" );

	logger->flush();

	const auto req_level { parseLevelString( parameters.level.value_or( "info" ) ) };

	auto ring_sink { log::getServerRingBufferSink() };
	if ( !ring_sink ) co_return http::Response::error( 500, "Could not find ring buffer sink in logger" );

	const auto raw_entries { ring_sink->last_raw( 0 ) };

	if ( format == "json" )
	{
		// Without an explicit limit every retained entry is returned.
		const auto limit { parameters.limit.value_or( std::numeric_limits< std::size_t >::max() ) };
		co_return renderLogJson( raw_entries, req_level, parameters.since, parameters.after, limit );
	}

	const auto formatter { log::makeServerFormatter() };

	std::string log_out {};
	log_out.reserve( 1024 * 8 );

	for ( const auto& entry : raw_entries )
	{
		if ( entry.level < req_level ) continue;

		if ( parameters.since.has_value() )
		{
			const auto entry_seconds {
				std::chrono::duration_cast< std::chrono::seconds >( entry.time.time_since_epoch() ).count()
			};
			if ( static_cast< std::uint64_t >( entry_seconds ) < *parameters.since ) continue;
		}

		spdlog::memory_buf_t buf;
		formatter->format( entry, buf );
		log_out += SPDLOG_BUF_TO_STRING( buf );
	}

	co_return http::EncodedResponse< std::string, "text/plain" > {
		http::Response::bytes( 200, folly::IOBuf::copyBuffer( log_out ), "text/plain" )
	};
}

http::HTTPTask< LogResponse > fetchLog( http::Query< FetchLogQuery > query )
{
	co_return co_await fetchLogImpl( query.value, false );
}

http::HTTPTask< LogResponse > fetchLogEntries( http::Query< FetchLogEntriesQuery > query )
{
	co_return co_await fetchLogImpl( query.value, true );
}

} // namespace idhan::httpapi
