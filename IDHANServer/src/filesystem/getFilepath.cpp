//
// Created by kj16609 on 10/30/25.
//

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"

namespace idhan::filesystem
{

ExpectedTask< std::filesystem::path > getFilepath( const RecordID record_id, DbClientPtr db )
{
	constexpr auto query { R"(
	SELECT sha256, best_extension, extension, folder_path FROM records
	LEFT JOIN file_info ON records.record_id = file_info.record_id
	LEFT JOIN mime ON file_info.mime_id = mime.mime_id
	LEFT JOIN file_clusters ON file_info.cluster_id = file_clusters.cluster_id
	WHERE records.record_id = $1
	)" };

	const auto result { co_await db->execSqlCoro( query, record_id ) };
	if ( result.empty() ) co_return std::unexpected( createBadRequest( "Invalid or Unstored record ID" ) );

	const auto& row { result[ 0 ] };
	const SHA256 hash { row[ "sha256" ] };
	if ( row[ "folder_path" ].isNull() )
		co_return std::unexpected( createNotFound( "Record {} has no files stored", record_id ) );

	const auto cluster_path { row[ "folder_path" ].as< std::string >() };

	const auto folder_path { row[ "folder_path" ].as< std::string >() };

	auto extension { row[ "best_extension" ].isNull() ?
		                 row[ "extension" ].as< std::string >() :
		                 row[ "best_extension" ].as< std::string >() };

	// remove leading . if present
	if ( extension.starts_with( '.' ) ) extension = extension.substr( 1 );

	const auto hash_string { hash.hex() };
	const std::string folder_name { format_ns::format( "f{}", hash_string.substr( 0, 2 ) ) };
	const std::string file_name { format_ns::format( "{}.{}", hash_string, extension ) };

	std::filesystem::path path { cluster_path };
	path /= folder_name;
	path /= file_name;

	co_return path;
}

} // namespace idhan::filesystem
