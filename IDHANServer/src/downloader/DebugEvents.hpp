#pragma once

#include "idhan/http/SSEEventHub.hpp"

namespace idhan::downloader
{
[[nodiscard]] http::SSEEventHub& debugEvents();
void notifyDebugChanged();
} // namespace idhan::downloader
