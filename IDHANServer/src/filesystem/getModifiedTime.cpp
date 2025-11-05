//
// Created by kj16609 on 11/4/25.
//

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace idhan::filesystem
{

std::int64_t getLastWriteTime( const std::filesystem::path& path )
{
	const auto file_mtime_local { std::filesystem::last_write_time( path ) };
	const auto file_mtime_unix { std::chrono::clock_cast< std::chrono::system_clock >( file_mtime_local ) };

	return std::chrono::duration_cast< std::chrono::microseconds >( file_mtime_unix.time_since_epoch() ).count();
}

} // namespace idhan::filesystem