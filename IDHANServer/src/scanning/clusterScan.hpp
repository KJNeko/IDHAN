#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "ScanOptions.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::scanning
{
struct ScanFailure
{
	std::filesystem::path path;
	std::string message;
};

struct ScanTotals
{
	std::uint64_t file_count {};
	std::uint64_t byte_size {};
	std::uint64_t failed_count {};
	std::vector< ScanFailure > failures {};
};

[[nodiscard]] runtime::Task< ScanTotals > scanCluster(
	db::DBContext& db,
	ClusterID cluster_id,
	std::filesystem::path configured_path,
	ScanOptions options );
} // namespace idhan::scanning
