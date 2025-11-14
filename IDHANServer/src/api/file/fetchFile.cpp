//
// Created by kj16609 on 3/22/25.
//

#include <regex>

#include "ServerContext.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getFastDbClient() };
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
		auto response { drogon::HttpResponse::newHttpResponse() };

		// add to response header that we support partial requests
		response->addHeader( "Accept-Ranges", "bytes" );

		response->addHeader( "Content-Length", std::to_string( file_size ) );

		const auto mime_info {
			co_await db->execSqlCoro( "SELECT mime.name as mime_name FROM file_info JOIN mime USING (mime_id)" )
		};

		if ( mime_info.empty() )
		{
			response->setContentTypeString( "application/octet-stream" );
			// response->addHeader( "Content-Type", "application/octet-stream" );
		}
		else
		{
			response->setContentTypeString( mime_info[ 0 ][ "mime_name" ].as< std::string >() );
			// response->addHeader( "Content-Type", mime_info[ 0 ][ "mime_name" ].as< std::string >() );
		}

		response->setPassThrough( true );

		co_return response;
	}

	// Get the header for ranges if supplied

	// Get the header for ranges if supplied
	const auto& range_header { request->getHeader( "Range" ) };
	std::size_t begin { 0 };
	std::size_t end { 0 };

	// This is stupid but apparently valid
	constexpr auto full_range { "bytes=0-" };

	const bool has_range_header { !range_header.empty() };
	const bool is_full_range { has_range_header && ( range_header == full_range ) };
	if ( !is_full_range && has_range_header )
	{
		constexpr auto regex_pattern { R"(bytes=(\d*)-(\d*)?)" };
		static const std::regex regex { regex_pattern };
		std::smatch range_match {};

		if ( std::regex_match( range_header, range_match, regex ) )
		{
			if ( range_match.size() != 3 )
			{
				log::error( "Invalid Range Header. Expected 3 matches, Got {}", range_match.size() );
				co_return createBadRequest( "Invalid Range Header. Expected 3 matches, Got {}", range_match.size() );
			}

			try
			{
				if ( range_match[ 1 ].matched )
				{
					log::debug( "Regex range header match 1: {}", range_match[ 1 ].str() );
					begin = static_cast< std::size_t >( std::stoull( range_match[ 1 ].str() ) );
				}
				if ( range_match[ 2 ].matched )
				{
					log::debug( "Regex range header match 2: {}", range_match[ 2 ].str() );
					end = static_cast< std::size_t >( std::stoull( range_match[ 2 ].str() ) );
				}
			}
			catch ( std::exception& e )
			{
				log::error( "Error with range header: {}, Header was {}", e.what(), range_header );
				co_return createBadRequest( "Error with range header: {}, Header was {}", e.what(), range_header );
			}

			// Ensure the range is valid
			if ( begin > end || end >= file_size ) co_return createBadRequest( "Invalid Range Header" );
		}
		else
		{
			co_return createBadRequest( "Invalid Range Header Format Regex failed: {}", regex_pattern );
		}
	}

	if ( request->getOptionalParameter< bool >( "download" ).value_or( false ) )
	{
		// send the file as a download instead of letting the browser try to display it
		const auto response { drogon::HttpResponse::newFileResponse( path_e->string(), path_e->filename().string() ) };
		co_return response;
	}

	auto response { drogon::HttpResponse::newFileResponse( path_e->string(), begin, end - begin ) };

	helpers::addFileCacheHeader(
		response /* max_age is set to 1 year, Since this is likely to never be changed by IDHAN */ );

	co_return response;
}

} // namespace idhan::api
