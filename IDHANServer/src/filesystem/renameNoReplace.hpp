#pragma once

#include <expected>
#include <filesystem>
#include <system_error>

namespace idhan::filesystem
{

// Atomically refuses an existing destination, including a dangling symlink. Both paths must be on
// the same filesystem; unsupported platforms or filesystems return an error without a fallback.
[[nodiscard]] std::expected< void, std::error_code > renameNoReplace(
	const std::filesystem::path& from,
	const std::filesystem::path& to );

} // namespace idhan::filesystem
