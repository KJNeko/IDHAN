#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace idhan::filesystem
{
// Removes recognized regular cache files only; directories and symlinks are retained.
[[nodiscard]] std::expected< std::size_t, std::string > clearThumbnailCache( const std::filesystem::path& path );
}
