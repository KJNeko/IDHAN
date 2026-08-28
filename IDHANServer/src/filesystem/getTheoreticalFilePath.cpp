#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "filesystem.hpp"
#include "logging/format_ns.hpp"
#include "threading/ExpectedTask.hpp"

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

	path /= getClusterRelativePath( sha256, extension );

	co_return path;
}
} // namespace idhan::filesystem
