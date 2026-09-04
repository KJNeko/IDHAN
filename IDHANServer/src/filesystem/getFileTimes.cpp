#include <sys/stat.h>

#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <optional>

#include "filesystem.hpp"

namespace idhan::filesystem
{

static std::chrono::system_clock::time_point toTimePoint( const statx_timestamp& ts )
{
	const auto since_epoch { std::chrono::seconds { ts.tv_sec } + std::chrono::nanoseconds { ts.tv_nsec } };

	return std::chrono::system_clock::time_point {
		std::chrono::duration_cast< std::chrono::system_clock::duration >( since_epoch )
	};
}

std::optional< FileTimes > getFileTimes( const std::filesystem::path& path )
{
	struct statx stx {};

	if ( ::statx( AT_FDCWD, path.c_str(), AT_STATX_SYNC_AS_STAT, STATX_MTIME | STATX_BTIME, &stx ) != 0 )
		return std::nullopt;

	FileTimes times { .mtime = toTimePoint( stx.stx_mtime ), .btime = std::nullopt };

	// The kernel leaves the bit clear on filesystems that record no birth time.
	if ( stx.stx_mask & STATX_BTIME ) times.btime = toTimePoint( stx.stx_btime );

	return times;
}

std::int64_t toMicroseconds( const std::chrono::system_clock::time_point time_point )
{
	return std::chrono::duration_cast< std::chrono::microseconds >( time_point.time_since_epoch() ).count();
}

std::optional< std::int64_t > toMicroseconds( const std::optional< std::chrono::system_clock::time_point > time_point )
{
	if ( !time_point ) return std::nullopt;

	return toMicroseconds( *time_point );
}

} // namespace idhan::filesystem
