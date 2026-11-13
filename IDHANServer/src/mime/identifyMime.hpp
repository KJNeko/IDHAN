#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::mime
{

//! `descriptor` must contain `bytes` and remain valid until the task completes.
[[nodiscard]] runtime::Task< MimeID > identifyMime(
	std::span< const std::byte > bytes,
	std::string_view filename,
	std::optional< int > descriptor = std::nullopt );

[[nodiscard]] runtime::Task< MimeID > identifyMimeForPath( const std::filesystem::path& path );

[[nodiscard]] runtime::Task< MimeID > identifyMimeForFd( int fd, std::size_t size, std::string_view filename );

} // namespace idhan::mime
