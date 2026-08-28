#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/utils/coroutine.h"
#include "filesystem/filesystem.hpp"
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

ExpectedTask< std::filesystem::path > getUncheckedRecordPath( const RecordID record_id, DbClientPtr db )
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

ExpectedTask< std::filesystem::path > getRecordPath( const RecordID record_id, DbClientPtr db )
{
	const auto file_location_e { co_await getUncheckedRecordPath( record_id, db ) };
	return_unexpected_error( file_location_e );
	const auto& file_location { *file_location_e };

	std::error_code status_error {};
	const auto file_status { std::filesystem::status( file_location, status_error ) };

	if ( status_error == std::errc::no_such_file_or_directory )
	{
		co_await reportMissingFile( record_id, file_location, db );
		co_return std::unexpected( createInternalError(
			"Record {} does not exist at the expected path '{}'.", record_id, file_location.string() ) );
	}

	if ( status_error )
	{
		co_return std::unexpected( createInternalError(
			"Could not inspect the expected path for record {} at '{}': {}",
			record_id,
			file_location.string(),
			status_error.message() ) );
	}

	if ( !std::filesystem::is_regular_file( file_status ) )
	{
		co_return std::unexpected( createInternalError(
			"Expected path for record {} is not a regular file: '{}'.", record_id, file_location.string() ) );
	}

	co_return file_location;
}
} // namespace idhan::filesystem
