#pragma once

#include <filesystem>

#include "IDHANTypes.hpp"
#include "MimeIDs.hpp"
#include "MimeReader.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan::mime
{

[[nodiscard]] IDHANTask< MimeID > prescanMime( MimeReader reader );

[[nodiscard]] IDHANTask< MimeID > prescanMimeForPath( std::filesystem::path path );

} // namespace idhan::mime
