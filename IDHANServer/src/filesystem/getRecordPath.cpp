#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan::filesystem
{

std::filesystem::path getFileFolder( const SHA256& sha256 )
{
	const auto hex { sha256.hex() };
	const auto folder_name { std::format( "f{}", hex.substr( 0, 2 ) ) };

	return folder_name;
}

std::filesystem::path getClusterRelativePath( const SHA256& sha256, std::string_view extension )
{
	if ( extension.starts_with( '.' ) ) extension.remove_prefix( 1 );

	const auto hex { sha256.hex() };
	const auto filename { extension.empty() ? hex : std::format( "{}.{}", hex, extension ) };

	return getFileFolder( sha256 ) / filename;
}

ExpectedTask< std::filesystem::path > getRecordPath( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro(

		R"(SELECT folder_path, sha256, COALESCE(extension, best_extension, '') as extension
				FROM records
						 JOIN file_info ON records.record_id = file_info.record_id
						 LEFT JOIN mime ON file_info.mime_id = mime.mime_id
						 JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id
				WHERE records.record_id = $1)",
		record_id ) };

	if ( result.empty() )
		co_return std::unexpected( createNotFound( "Record {} is not stored in any cluster", record_id ) );

	const std::filesystem::path folder_path { result[ 0 ][ 0 ].as< std::string >() };
	const SHA256 sha256 { SHA256::fromPgCol( result[ 0 ][ 1 ] ) };
	const std::string mime_extension { result[ 0 ][ 2 ].as< std::string >() };

	const auto file_location { folder_path / getClusterRelativePath( sha256, mime_extension ) };

	co_return file_location;
}
} // namespace idhan::filesystem
