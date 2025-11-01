//
// Created by kj16609 on 10/30/25.
//

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/format_ns.hpp"
#include "utility.hpp"

namespace idhan::filesystem
{
ExpectedTask< std::filesystem::path > getTheoreticalFilePath(
	const ClusterID cluster_id,
	const SHA256 sha256,
	std::string extension )
{
	const auto cluster_path_e { co_await getClusterPath( cluster_id ) };
	return_unexpected_error( cluster_path_e );

	std::filesystem::path path { *cluster_path_e };

	const auto hash_hex { sha256.hex() };
	// folder name is f00 to fff
	const auto folder_name { format_ns::format( "f{}", hash_hex.substr( 0, 2 ) ) };

	// kill starting `.`
	if ( extension.starts_with( "." ) ) extension = extension.substr( 1 );

	const auto file_name { extension.empty() ? hash_hex : format_ns::format( "{}.{}", hash_hex, extension ) };

	path /= folder_name;
	path /= file_name;

	co_return path;
}
} // namespace idhan::filesystem
