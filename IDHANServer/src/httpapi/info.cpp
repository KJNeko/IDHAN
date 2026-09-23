#include "info.hpp"

#include <folly/io/IOBuf.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <format>
#include <string>
#include <string_view>

#include "apiAuth.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/FileResponse.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

static http::HTTPTask< http::Response > apiDocsIndex( [[maybe_unused]] http::RequestContext context )
{
	const auto file_path { getStaticPath() / "apidocs.html" };

	auto file_e { folly::File::makeFile( file_path.c_str(), O_RDONLY ) };
	if ( !file_e ) co_return http::Response::error( 404, "API docs are not built into this server" );

	auto file { std::move( *file_e ) };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 || !S_ISREG( status.st_mode ) )
		co_return http::Response::error( 404, "API docs are not built into this server" );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = std::format( "{:x}-{:x}", status.st_mtime, status.st_size ),
		.content_type = "text/html; charset=utf-8",
		.filename = "apidocs.html",
	};

	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
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

static http::HTTPTask< http::Response > fetchLog( http::RequestContext context )
{
	const auto level_e { context.parameters().optional< std::string >( "level" ) };
	if ( !level_e ) co_return http::Response::error( 400, level_e.error().message );

	const auto since_e { context.parameters().optional< std::uint64_t >( "since" ) };
	if ( !since_e ) co_return http::Response::error( 400, since_e.error().message );

	auto logger { log::getServerLogger() };
	if ( !logger ) co_return http::Response::error( 500, "Could not get default spdlog logger" );

	logger->flush();

	const auto req_level { parseLevelString( level_e->value_or( "info" ) ) };

	auto ring_sink { log::getServerRingBufferSink() };
	if ( !ring_sink ) co_return http::Response::error( 500, "Could not find ring buffer sink in logger" );

	constexpr std::string_view server_fmt { "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v" };
	spdlog::pattern_formatter formatter { std::string( server_fmt ) };

	const auto raw_entries { ring_sink->last_raw( 0 ) };

	std::string log_out {};
	log_out.reserve( 1024 * 8 );

	for ( const auto& entry : raw_entries )
	{
		if ( entry.level < req_level ) continue;

		if ( since_e->has_value() )
		{
			const auto entry_seconds {
				std::chrono::duration_cast< std::chrono::seconds >( entry.time.time_since_epoch() ).count()
			};
			if ( static_cast< std::uint64_t >( entry_seconds ) < **since_e ) continue;
		}

		spdlog::memory_buf_t buf;
		formatter.format( entry, buf );
		log_out += SPDLOG_BUF_TO_STRING( buf );
	}

	co_return http::Response::bytes( 200, folly::IOBuf::copyBuffer( log_out ), "text/plain" );
}

http::RouteDefinition apiDocsIndexRoute()
{
	return http::RouteDefinition { http::RouteMetadata { proxygen::HTTPMethod::GET, "/api", {}, {} }, apiDocsIndex };
}

http::RouteDefinition logRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/log", {}, { apiAuthValidation( db ) } }, fetchLog
	};
}

} // namespace idhan::httpapi
