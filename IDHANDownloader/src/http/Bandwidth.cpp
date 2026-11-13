#include "IDHANDownloader/http/Bandwidth.hpp"

#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>

namespace idhan::downloader
{
static std::mutex bandwidth_mutex;
static std::deque< BandwidthLog > bandwidth_logs;

void recordBandwidth( BandwidthLog log )
{
	const std::scoped_lock lock { bandwidth_mutex };
	bandwidth_logs.push_back( std::move( log ) );
}

std::vector< BandwidthLog > takeBandwidthLogs( const std::size_t limit )
{
	const std::scoped_lock lock { bandwidth_mutex };
	const auto count { std::min( limit, bandwidth_logs.size() ) };
	std::vector< BandwidthLog > result;
	result.reserve( count );
	for ( std::size_t index {}; index < count; ++index )
	{
		result.push_back( std::move( bandwidth_logs.front() ) );
		bandwidth_logs.pop_front();
	}
	return result;
}
} // namespace idhan::downloader
