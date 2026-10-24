#pragma once

#include <filesystem>

#include "idhan/SHA256.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::filesystem
{
// Reads in bounded chunks; the caller must keep any pinned cluster path alive until completion.
[[nodiscard]] runtime::Task< SHA256 > hashFile( std::filesystem::path path );
} // namespace idhan::filesystem
