#pragma once

#include <expected>
#include <string>

namespace idhan::filesystem
{
[[nodiscard]] std::expected< std::size_t, std::string > clearThumbnailCache();
}
