//
// Created by kj16609 on 3/22/25.
//

#include <regex>

#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "filesystem/utility.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchFile(
	drogon::HttpRequestPtr request,
	RecordID record_id,
	DbClientPtr db )
{
	const auto path_e { co_await filesystem::getFilepath( record_id, db ) };
	if ( !path_e ) co_return path_e.error();

	if ( !std::filesystem::exists( *path_e ) )
	{
		log::warn( "Expected file at location {} for record {} but no file was found", path_e->string(), record_id );
		co_return createInternalError( "File was expected but not found. Possible data loss" );
	}

	// Check if this is a head request
	if ( request->isHead() )
	{
		auto response { drogon::HttpResponse::newHttpResponse() };

		// add to response header that we support partial requests
		response->addHeader( "Accept-Ranges", "bytes" );
		response->addHeader( "Content-Length", std::to_string( std::filesystem::file_size( *path_e ) ) );

		co_return response;
	}

	// Get the header for ranges if supplied

	const std::size_t file_size { std::filesystem::file_size( *path_e ) };

	// Get the header for ranges if supplied
	const auto& range_header { request->getHeader( "Range" ) };
	std::size_t begin { 0 };
	std::size_t end { 0 };

	// This is stupid but apparently valid
	constexpr auto full_range { "bytes=0-" };
	if ( !range_header.empty() && range_header != full_range )
	{
		static const std::regex range_pattern { R"(bytes=(\d*)-(\d*)?)" };
		std::smatch range_match {};

		if ( std::regex_match( range_header, range_match, range_pattern ) )
		{
			if ( range_match.size() != 3 )
			{
				log::error( "Invalid Range Header. Expected 3 matches, Got {}", range_match.size() );
				co_return createBadRequest( "Invalid Range Header. Expected 3 matches, Got {}", range_match.size() );
			}

			try
			{
				if ( range_match[ 1 ].matched )
					begin = static_cast< std::size_t >( std::stoull( range_match[ 1 ].str() ) );
				if ( range_match[ 2 ].matched )
					end = static_cast< std::size_t >( std::stoull( range_match[ 2 ].str() ) );
			}
			catch ( std::exception& e )
			{
				log::error( "Error with range header: {}", e.what() );
				co_return createBadRequest( "Invalid Range Header" );
			}

			// Ensure the range is valid
			if ( begin > end || end >= file_size ) co_return createBadRequest( "Invalid Range Header" );
		}
		else
		{
			co_return createBadRequest( "Invalid Range Header Format" );
		}
	}

	if ( request->getOptionalParameter< bool >( "download" ).value_or( false ) )
	{
		// send the file as a download instead of letting the browser try to display it
		co_return drogon::HttpResponse::newFileResponse( path_e->string(), path_e->filename().string() );
	}

	auto response { drogon::HttpResponse::newFileResponse( path_e->string(), begin, end - begin ) };

	helpers::addFileCacheHeader(
		response /* max_age is set to 1 year, Since this is likely to never be changed by IDHAN */ );

	co_return response;
}

} // namespace idhan::api
