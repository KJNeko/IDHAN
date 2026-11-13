#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace idhan::downloader
{

struct BandwidthLog
{
	std::string host;
	std::int64_t timestamp {};
	std::int64_t bytes_received {};
};

void recordBandwidth( BandwidthLog log );
[[nodiscard]] std::vector< BandwidthLog > takeBandwidthLogs( std::size_t limit = 4096 );

} // namespace idhan::downloader
