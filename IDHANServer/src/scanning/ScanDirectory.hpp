#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>

#include "clusterScan.hpp"

namespace idhan::scanning
{
struct ScanCandidate
{
	std::filesystem::path path;
	std::uint64_t size {};
};

// Only the coordinator advances these iterators. Workers receive owned candidates.
class ScanDirectory
{
	std::filesystem::path m_root;
	std::filesystem::directory_iterator m_folders {};
	std::filesystem::directory_iterator m_files {};
	std::optional< ScanFailure > m_failure {};
	bool m_advance_file { false };
	bool m_advance_folder { false };

  public:

	explicit ScanDirectory( std::filesystem::path root );
	[[nodiscard]] std::expected< std::optional< ScanCandidate >, ScanFailure > next();
};
} // namespace idhan::scanning
