#pragma once

// Use format_ns at call sites to support both std::format and {fmt}.
#ifdef IDHAN_USE_STD_FORMAT
#include <format>
#include "logging/std_formatters/path.hpp"
namespace format_ns = std;
#else
#include <fmt/format.h>
namespace format_ns = fmt;
#endif
