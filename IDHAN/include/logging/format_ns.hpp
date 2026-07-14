//
// Created by kj16609 on 7/16/25.
//
#pragma once

//! Alias for the active formatting library: std::format when IDHAN_USE_STD_FORMAT is defined,
//! otherwise {fmt}. Use format_ns::format everywhere so call sites don't hard-code either backend.
#ifdef IDHAN_USE_STD_FORMAT
#include <format>
namespace format_ns = std;
#else
#include <fmt/format.h>
namespace format_ns = fmt;
#endif
