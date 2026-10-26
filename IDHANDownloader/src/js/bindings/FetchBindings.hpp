#pragma once

#include <expected>
#include <quickjs.h>
#include <string>

namespace idhan::downloader
{
[[nodiscard]] std::expected< void, std::string > installFetchBindings( JSContext* context );
} // namespace idhan::downloader
