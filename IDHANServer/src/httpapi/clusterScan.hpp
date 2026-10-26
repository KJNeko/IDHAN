#pragma once

#include <cstddef>
#include <filesystem>

#include "IDHANTypes.hpp"
#include "jobs/JobTask.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct ClusterScanParams
{
	bool read_only { true };
	bool force_readonly { false };
	bool verify_hash { false };
	bool adopt_orphans { false };
	bool scan_mime { true };
	bool rescan_mime { false };
	bool scan_metadata { true };
	bool rescan_metadata { false };
	bool stop_on_fail { false };
	bool track_missing_files { false };
	bool fix_extensions { false };
	std::size_t concurrency { 4 };
};

[[nodiscard]] JobTask scanClusterJob(
	ClusterID cluster_id,
	std::filesystem::path cluster_path,
	ClusterScanParams params,
	db::DBContext& db );

} // namespace idhan::httpapi
