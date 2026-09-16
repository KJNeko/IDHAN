#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace idhan
{
class SHA256;
}

namespace idhan::filesystem
{

[[nodiscard]] std::filesystem::path getClusterRelativePath( const SHA256& sha256, std::string_view extension );

struct FileTimes
{
	std::chrono::system_clock::time_point mtime;
	std::optional< std::chrono::system_clock::time_point > btime;
};

[[nodiscard]] std::optional< FileTimes > getFileTimes( const std::filesystem::path& path );
[[nodiscard]] std::int64_t toMicroseconds( std::chrono::system_clock::time_point time_point );
[[nodiscard]] std::optional< std::int64_t > toMicroseconds(
	std::optional< std::chrono::system_clock::time_point > time_point );

} // namespace idhan::filesystem
