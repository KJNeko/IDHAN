#pragma once

#include <expected>
#include <quickjs.h>
#include <string>

namespace idhan::downloader
{
[[nodiscard]] std::expected< void, std::string > installURLPatternBindings( JSContext* context );
}
