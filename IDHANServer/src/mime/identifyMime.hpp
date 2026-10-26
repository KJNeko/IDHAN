#pragma once

#include <filesystem>
#include <span>
#include <string_view>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::mime
{

[[nodiscard]] runtime::Task< MimeID > identifyMime( std::span< const std::byte > bytes, std::string_view filename );

[[nodiscard]] runtime::Task< MimeID > identifyMimeForPath( const std::filesystem::path& path );

[[nodiscard]] runtime::Task< MimeID > identifyMimeForFd( int fd, std::size_t size, std::string_view filename );

} // namespace idhan::mime
