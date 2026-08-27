#pragma once

#include <filesystem>

#include "fgl/defines.hpp"
#include "logging/log.hpp"

#ifdef __linux__
#ifndef IDHAN_STATIC_PATH
#define IDHAN_STATIC_PATH "/usr/share/idhan/static"
#endif


#ifndef IDHAN_MODULES_PATH
#define IDHAN_MODULES_PATH "/usr/share/idhan/modules"
#endif

#ifndef IDHAN_MODULE_RUNNER_PATH
#define IDHAN_MODULE_RUNNER_PATH "/usr/bin/IDHANModuleRunner"
#endif

#elif defined( _WIN32 )
#ifndef IDHAN_STATIC_PATH
#define IDHAN_STATIC_PATH "C:\\ProgramData\\IDHAN\\static"
#endif


#ifndef IDHAN_MODULES_PATH
#define IDHAN_MODULES_PATH "C:\\ProgramData\\IDHAN\\modules"
#endif

#ifndef IDHAN_MODULE_RUNNER_PATH
#define IDHAN_MODULE_RUNNER_PATH "C:\\ProgramData\\IDHAN\\IDHANModuleRunner.exe"
#endif

#else
#error "No paths supplied for finding IDHAN info. Unsupported OS"
#endif

namespace idhan
{

std::filesystem::path getExecutableDir();

std::vector< std::filesystem::path > getModulePaths();

std::filesystem::path getModuleRunnerPath();

std::filesystem::path getStaticPath();
std::filesystem::path getThumbnailsPath();

//! Requests outside `[thumbnails] cacheable_sizes` are generated but not cached. Read live per miss.
const std::vector< std::size_t >& getCacheableThumbnailSizes();

bool getThumbnailCachingEnabled();

//! `[thumbnails] purge_on_boot` applies at startup only.
bool getPurgeThumbnailsOnBoot();

//! `[plugins] path` must still be reachable under `/plugins/...` for the browser to fetch bundles.
std::filesystem::path getPluginsPath();

} // namespace idhan
