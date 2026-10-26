#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace idhan::config
{
// A true recovery acknowledgement must come from a writable TOML file. It is
// atomically reset to false before use, including when the recovery attempt fails.
[[nodiscard]] std::expected< bool, std::string > consumeFlag( std::string_view group, std::string_view name );
} // namespace idhan::config
