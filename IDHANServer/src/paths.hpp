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
#else
#error "No paths supplied for finding IDHAN info. Likely incompatable OS"
#endif

namespace idhan
{

std::vector< std::filesystem::path > getModulePaths();
std::vector< std::filesystem::path > getMimeParserPaths();
std::filesystem::path getStaticPath();
std::filesystem::path getThumbnailsPath();

} // namespace idhan
