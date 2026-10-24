#pragma once

#include <cstddef>

namespace idhan::scanning
{
enum class ScanMode
{
	Disabled,
	MissingOnly,
	Always
};

struct ScanOptions
{
	bool read_only { true };
	bool verify_hash { false };
	bool adopt_orphans { false };
	ScanMode mime { ScanMode::MissingOnly };
	ScanMode metadata { ScanMode::MissingOnly };
	bool stop_on_fail { false };
	bool track_missing_files { false };
	bool fix_extensions { false };
	std::size_t concurrency { 4 };
};
} // namespace idhan::scanning
