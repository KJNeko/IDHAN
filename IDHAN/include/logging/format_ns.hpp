//
// Created by kj16609 on 7/16/25.
//
#pragma once

#ifdef IDHAN_USE_STD_FORMAT
#include <format>
namespace format_ns = std;
#define SPDLOG_USE_STD_FORMAT 1
#else
#include <fmt/format.h>
namespace format_ns = fmt;
#endif
