#include <chrono>
#include <cstddef>
#include <filesystem>

namespace idhan::filesystem
{

std::chrono::system_clock::time_point getLastWriteTime( const std::filesystem::path& path )
{
	const auto file_mtime_local { std::filesystem::last_write_time( path ) };

	return std::chrono::clock_cast< std::chrono::system_clock >( file_mtime_local );
}

} // namespace idhan::filesystem
