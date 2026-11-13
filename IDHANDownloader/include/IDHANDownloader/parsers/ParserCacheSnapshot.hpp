#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace idhan::downloader
{

struct ParserCacheEntry
{
	std::string parser {};
	std::string key {};
	std::uint64_t hits {};
	std::int64_t lifetime_seconds {};
	std::int64_t expires_in_seconds {};
};

struct ParserCacheSnapshot
{
	//! Totals include expired/deleted entries and reset with the downloader.
	std::uint64_t hits {};
	std::uint64_t misses {};
	double hit_rate {};
	std::size_t entry_count {};
	//! Live keys ordered by descending hits, then parser and key. Values are omitted.
	std::vector< ParserCacheEntry > entries {};
};

} // namespace idhan::downloader
