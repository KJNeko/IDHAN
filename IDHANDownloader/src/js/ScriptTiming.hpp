#pragma once

#include <chrono>
#include <string>

namespace idhan::downloader
{
//! Uses the largest applicable unit and truncates to one decimal place.
[[nodiscard]] std::string formatScriptDuration( std::chrono::nanoseconds elapsed );
} // namespace idhan::downloader
