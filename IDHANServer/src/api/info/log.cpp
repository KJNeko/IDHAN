//
// Created by kj16609 on 3/26/26.
//

#include <chrono>

#include "Config.hpp"
#include "api/InfoAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

auto parseLevelString( const std::string& level_str )
{
	if ( level_str == "trace" ) return spdlog::level::trace;
	if ( level_str == "debug" ) return spdlog::level::debug;
	if ( level_str == "info" ) return spdlog::level::info;
	if ( level_str == "warning" || level_str == "warn" ) return spdlog::level::warn;
	if ( level_str == "error" || level_str == "err" ) return spdlog::level::err;
	if ( level_str == "critical" ) return spdlog::level::critical;
	return spdlog::level::info;
}

//FORMAT: [YYYY-MM-DD HH-MM-SS.TTTT] [SERVER] [level] [thread <>] <MSG>

bool logEntryMatchesLevel( const std::string& log_entry, const spdlog::level::level_enum level )
{
	// Extract level from log entry format: [YYYY-MM-DD HH-MM-SS.TTTT] [SERVER] [level] [thread <>] <MSG>
	const auto level_start = log_entry.find( "] [" );
	if ( level_start == std::string::npos ) return false;

	const auto level_start_pos = level_start + 3; // Skip "] ["
	const auto level_end = log_entry.find( ']', level_start_pos );
	if ( level_end == std::string::npos ) return false;

	const std::string entry_level_str = log_entry.substr( level_start_pos, level_end - level_start_pos );
	const auto entry_level = parseLevelString( entry_level_str );

	// Include higher levels: warn includes error, info includes error and warn, etc.
	return entry_level >= level;
}

bool logEntryMatchesTime( const std::string& log_entry, const auto since_ms )
{
	const auto time_start = log_entry.find_first_of( '[' );
	const auto time_end = log_entry.find_first_of( ']' );
	const auto time_string = log_entry.substr( time_start + 1, time_end - time_start - 1 );

	const auto sub_seconds_start = time_string.find_first_of( '.' );
	const std::string time_sanitized = time_string.substr( 0, sub_seconds_start ); // "YYYY-MM-DD HH:MM:SS"

	std::tm tm {};
	std::istringstream ss( time_sanitized );
	ss >> std::get_time( &tm, "%Y-%m-%d %H:%M:%S" );
	if ( ss.fail() ) return false;

	std::time_t t { std::mktime( &tm ) };
	unsigned long log_time_ms { static_cast< unsigned long >( t ) };

	return since_ms.has_value() ? log_time_ms >= since_ms.value() : true;
}

drogon::Task< drogon::HttpResponsePtr > InfoAPI::log( drogon::HttpRequestPtr request )
{
	// /log?since=<unix_timestamp>&level=<level>

	auto logger { spdlog::get( "default" ) };
	logger->flush();

	const auto level_str { request->getOptionalParameter< std::string >( "level" ) };
	const auto log_level { parseLevelString( level_str.value_or( "info" ) ) };
	const auto since_str { request->getOptionalParameter< std::uint64_t >( "since" ) };

	std::vector< std::string > log_entries {};

	const auto log_path { config::getLogPath() };

	const auto info_log_file { log_path / "info.log" };
	const auto error_log_file { log_path / "error.log" };

	if ( std::ifstream ifs( info_log_file, std::ios::binary ); ifs )
	{
		log::debug( "Loading {}", info_log_file.string() );
		while ( !ifs.eof() && ifs.good() )
		{
			std::string line {};
			std::getline( ifs, line );
			log_entries.emplace_back( line );
		}

		if ( !ifs.eof() )
		{
			co_return createInternalError( "Failed to read info file, Reached abnormal state" );
		}
	}

	if ( std::ifstream ifs( error_log_file, std::ios::binary ); ifs )
	{
		log::debug( "Loading {}", error_log_file.string() );
		while ( !ifs.eof() && ifs.good() )
		{
			std::string line {};
			std::getline( ifs, line );
			log_entries.emplace_back( line );
		}

		if ( !ifs.eof() )
		{
			co_return createInternalError( "Failed to read error file, Reached abnormal state" );
		}
	}

	std::string log_out {};
	log_out.reserve( 1024 * 8 );

	for ( const auto& log_line : log_entries )
	{
		if ( !logEntryMatchesLevel( log_line, log_level ) ) continue;

		if ( !logEntryMatchesTime( log_line, since_str ) ) continue;

		log_out += log_line;
		log_out += "\n";
	}

	auto response { drogon::HttpResponse::newHttpResponse() };
	response->setBody( log_out );
	response->setStatusCode( drogon::HttpStatusCode::k200OK );
	response->setContentTypeCode( drogon::CT_TEXT_PLAIN );

	co_return response;
}

} // namespace idhan::api