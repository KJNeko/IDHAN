#pragma once

#include <filesystem>

#include "IDHANTypes.hpp"
#include "MimeIDs.hpp"
#include "MimeReader.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::mime
{

[[nodiscard]] runtime::Task< MimeID > prescanMime( MimeReader reader );

[[nodiscard]] runtime::Task< MimeID > prescanMimeForPath( std::filesystem::path path );

[[nodiscard]] runtime::Task< MimeID > prescanMimeForFd( int fd, std::size_t size );

} // namespace idhan::mime
