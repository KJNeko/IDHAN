//
// Created by kj16609 on 3/22/25.
//

#include <limits>
#include <regex>

#include "ServerContext.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > createHttpHeadForFile(
	const drogon::orm::DbClientPtr db,
	const std::size_t file_size,
	const RecordID record_id )
{
	auto response { drogon::HttpResponse::newHttpResponse() };

	// add to response header that we support partial requests
	response->addHeader( "Accept-Ranges", "bytes" );

	response->addHeader( "Content-Length", std::to_string( file_size ) );

	const auto mime_info {
		co_await db->execSqlCoro(
			"SELECT mime.name as mime_name FROM file_info JOIN mime USING (mime_id) WHERE file_info.record_id = $1",
			record_id )
	};

	if ( mime_info.empty() )
	{
		response->setContentTypeString( "application/octet-stream" );
	}
	else
	{
		response->setContentTypeString( mime_info[ 0 ][ "mime_name" ].as< std::string >() );
	}

	response->setPassThrough( true );
	co_return response;
}

std::optional< drogon::HttpResponsePtr > parseRangeHeader(
	const std::size_t file_size,
	const std::string& range_header,
	std::size_t& begin,
	std::size_t& end_pos )
{
	constexpr auto regex_pattern { R"(bytes=(\d+)-(\d*)?)" };
	static const std::regex regex { regex_pattern };
	std::smatch range_match {};

	if ( std::regex_match( range_header, range_match, regex ) )
	{
		if ( range_match.size() != 3 )
		{
			log::error( "Invalid Range Header. Expected 3 matches, Got {}", range_match.size() );
			return createBadRequest( "Invalid Range Header. Expected 3 matches, Got {}", range_match.size() );
		}

		try
		{
			log::debug( "Regex range header match 1: {}", range_match[ 1 ].str() );
			begin = static_cast< std::size_t >( std::stoull( range_match[ 1 ].str() ) );

			// Empty group 2 means open-ended range (bytes=N-); leave end_pos as sentinel
			if ( range_match[ 2 ].matched && !range_match[ 2 ].str().empty() )
			{
				log::debug( "Regex range header match 2: {}", range_match[ 2 ].str() );
				end_pos = static_cast< std::size_t >( std::stoull( range_match[ 2 ].str() ) );
			}
		}
		catch ( std::exception& e )
		{
			log::error( "Error with range header: {}, Header was {}", e.what(), range_header );
			return createBadRequest( "Error with range header: {}, Header was {}", e.what(), range_header );
		}

		// Ensure the range is valid
		if ( end_pos != std::numeric_limits< std::size_t >::max() )
		{
			if ( begin > end_pos || end_pos >= file_size ) return createBadRequest( "Invalid Range Header" );
		}
		else if ( begin >= file_size )
		{
			return createBadRequest( "Invalid Range Header" );
		}
	}
	else
	{
		return createBadRequest( "Invalid Range Header Format Regex failed: {}", regex_pattern );
	}

	return std::nullopt;
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };
	const auto path_e { co_await filesystem::getRecordPath( record_id, db ) };
	if ( !path_e ) co_return path_e.error();

	if ( !std::filesystem::exists( *path_e ) )
	{
		log::warn( "Expected file at location {} for record {} but no file was found", path_e->string(), record_id );
		co_return createInternalError(
			"File not found at expected location. Record ID: {}, Path: {}. This may indicate data corruption or file system issues.",
			record_id,
			path_e->string() );
	}

	const std::size_t file_size { std::filesystem::file_size( *path_e ) };

	// Check if this is a head request
	if ( request->isHead() )
	{
		co_return co_await createHttpHeadForFile( db, file_size, record_id );
	}

	// Get the header for ranges if supplied
	const auto& range_header { request->getHeader( "Range" ) };
	std::size_t begin { 0 };
	std::size_t end_pos { std::numeric_limits< std::size_t >::max() }; // max = open-ended (serve to EOF)

	const bool has_range_header { !range_header.empty() };

	// This is stupid but apparently valid
	constexpr auto full_range { "bytes=0-" };
	const bool is_full_range { has_range_header && ( range_header == full_range ) };

	if ( !is_full_range && has_range_header ) // needs to parse
	{
		if ( auto value = parseRangeHeader( file_size, range_header, begin, end_pos ); value ) co_return *value;
	}

	if ( request->getOptionalParameter< bool >( "download" ).value_or( false ) )
	{
		// send the file as a download instead of letting the browser try to display it
		const auto response { drogon::HttpResponse::newFileResponse( path_e->string(), path_e->filename().string() ) };
		co_return response;
	}

	// length=0 means "serve from begin to EOF" in Drogon; use it for no-range and open-ended ranges
	const std::size_t length { end_pos != std::numeric_limits< std::size_t >::max() ? end_pos - begin + 1 : 0 };
	auto response { drogon::HttpResponse::newFileResponse( path_e->string(), begin, length ) };

	helpers::addFileCacheHeader(
		response /* max_age is set to 1 year, Since this is likely to never be changed by IDHAN */ );

	co_return response;
}

} // namespace idhan::api
