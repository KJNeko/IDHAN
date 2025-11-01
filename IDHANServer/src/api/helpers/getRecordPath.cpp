//
// Created by kj16609 on 6/12/25.
//

#include "createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/utils/coroutine.h"
#include "helpers.hpp"

namespace idhan::api::helpers
{
drogon::Task< std::expected< std::filesystem::path, drogon::HttpResponsePtr > >
	getRecordPath( const RecordID record_id, DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro(

		R"(SELECT folder_path, sha256, COALESCE(extension, best_extension) as extension
				FROM records
						 JOIN file_info ON records.record_id = file_info.record_id
						 LEFT JOIN mime ON file_info.mime_id = mime.mime_id
						 JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id
				WHERE records.record_id = $1)",
		record_id ) };

	if ( result.empty() )
		co_return std::unexpected( createBadRequest( "Record {} is not stored in any cluster", record_id ) );

	const std::filesystem::path folder_path { result[ 0 ][ 0 ].as< std::string >() };
	const SHA256 sha256 { SHA256::fromPgCol( result[ 0 ][ 1 ] ) };
	const std::string mime_extension { result[ 0 ][ 2 ].as< std::string >() };

	const auto hex { sha256.hex() };
	const std::filesystem::path file_location { folder_path / std::format( "f{}", hex.substr( 0, 2 ) )
		                                        / ( std::format( "{}.{}", hex, mime_extension ) ) };

	co_return file_location;
}
} // namespace idhan::api::helpers
