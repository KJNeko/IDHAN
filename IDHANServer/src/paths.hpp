//
// Created by kj16609 on 10/13/25.
//
#pragma once

#include <filesystem>

#include "fgl/defines.hpp"
#include "logging/log.hpp"

#ifdef __linux__
#ifndef IDHAN_STATIC_PATH
#define IDHAN_STATIC_PATH "/usr/share/idhan/static"
#endif

#ifndef IDHAN_MIME_PATH
#define IDHAN_MIME_PATH "/usr/share/idhan/mime"
#endif

#ifndef IDHAN_MODULES_PATH
#define IDHAN_MODULES_PATH "/usr/share/idhan/modules"
#endif

#elif defined( _WIN32 )
#ifndef IDHAN_STATIC_PATH
#define IDHAN_STATIC_PATH "C:\\ProgramData\\IDHAN\\static"
#endif

#ifndef IDHAN_MIME_PATH
#define IDHAN_MIME_PATH "C:\\ProgramData\\IDHAN\\mime"
#endif

#ifndef IDHAN_MODULES_PATH
#define IDHAN_MODULES_PATH "C:\\ProgramData\\IDHAN\\modules"
#endif

#else
#error "No paths supplied for finding IDHAN info. Unsupported OS"
#endif

namespace idhan
{

std::vector< std::filesystem::path > getModulePaths();
std::vector< std::filesystem::path > getMimeParserPaths();
std::filesystem::path getStaticPath();
std::filesystem::path getThumbnailsPath();

//! Square thumbnail edge lengths (px) the server is permitted to write to its on-disk cache. Requests
//! for other sizes are still generated and served, just never cached. Config: `[thumbnails] cacheable_sizes`.
//! Read live per call so an operator can change it without restarting; only hit on a cache miss.
std::vector< std::size_t > getCacheableThumbnailSizes();

} // namespace idhan
