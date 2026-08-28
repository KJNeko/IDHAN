#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "IDHANTypes.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::mime
{

[[nodiscard]] IDHANTask< MimeID > identifyMime( std::span< const std::byte > bytes, std::string_view filename );

[[nodiscard]] IDHANTask< MimeID > identifyMimeForPath( const std::filesystem::path& path );

} // namespace idhan::mime
