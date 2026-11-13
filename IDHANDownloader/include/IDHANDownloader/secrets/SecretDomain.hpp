#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace idhan::downloader
{

// Canonicalizes an explicitly configured bare hostname. Does not infer a parent domain.
[[nodiscard]] std::expected< std::string, std::string > secretDomain( std::string_view domain );

} // namespace idhan::downloader
