//
// Created by kj16609 on 3/22/25.
//

#include <regex>

#include "api/IDHANRecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::fetchFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	constexpr auto query { R"(
	SELECT sha256, best_extension, extension, folder_path FROM records
	LEFT JOIN file_info ON records.record_id = file_info.record_id
	LEFT JOIN mime ON file_info.mime_id = mime.mime_id
	LEFT JOIN file_clusters ON file_info.cluster_id = file_clusters.cluster_id
	WHERE records.record_id = $1
	)" };

	const auto result { co_await db->execSqlCoro( query, record_id ) };
	if ( result.empty() ) co_return createBadRequest( "Invalid record id" );

	const auto& row { result[ 0 ] };
	const SHA256 hash { row[ "sha256" ] };
	if ( row[ "folder_path" ].isNull() ) co_return createNotFound( "Record {} has no files stored", record_id );
	const auto cluster_path { row[ "folder_path" ].as< std::string >() };

	const auto folder_path { row[ "folder_path" ].as< std::string >() };
	auto extension { row[ "best_extension" ].isNull() ? row[ "extension" ].as< std::string >() :
		                                                row[ "best_extension" ].as< std::string >() };

	if ( extension.starts_with( '.' ) ) extension = extension.substr( 1 );

	const auto hash_string { hash.hex() };
	const std::string folder { std::format( "f{}", hash_string.substr( 0, 2 ) ) };
	const std::string file { std::format( "{}.{}", hash_string, extension ) };

	std::filesystem::path path { cluster_path };
	path /= folder;
	path /= file;

	if ( !std::filesystem::exists( path ) )
	{
		log::warn( "Expected file at location {} for record {} but no file was found", path.string(), record_id );
		co_return createInternalError( "File was expected but not found. Possible data loss" );
	}

	// Check if this is a head request
	if ( request->isHead() )
	{
		auto response { drogon::HttpResponse::newHttpResponse() };

		// add to response header that we support partial requests
		response->addHeader( "Accept-Ranges", "bytes" );
		response->addHeader( "Content-Length", std::to_string( std::filesystem::file_size( path ) ) );

		co_return response;
	}

	// Get the header for ranges if supplied

	const std::size_t file_size { std::filesystem::file_size( path ) };

	// Get the header for ranges if supplied
	const auto& range_header { request->getHeader( "Range" ) };
	std::size_t begin { 0 };
	std::size_t end { 0 };

	if ( !range_header.empty() )
	{
		static const std::regex range_pattern { R"(bytes=(\d*)-(\d*)?)" };
		std::smatch range_match {};

		if ( std::regex_match( range_header, range_match, range_pattern ) )
		{
			if ( range_match[ 1 ].matched ) begin = static_cast< std::size_t >( std::stoull( range_match[ 1 ] ) );
			if ( range_match[ 2 ].matched ) end = static_cast< std::size_t >( std::stoull( range_match[ 2 ] ) );

			// Ensure the range is valid
			if ( begin > end || end >= file_size ) co_return createBadRequest( "Invalid Range Header" );
		}
		else
		{
			co_return createBadRequest( "Invalid Range Header Format" );
		}
	}

	auto response { drogon::HttpResponse::newFileResponse( path.string(), begin, end - begin ) };

	helpers::addFileCacheHeader( response );

	co_return response;
}



} // namespace idhan::api
