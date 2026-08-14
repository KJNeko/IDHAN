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

#ifndef IDHAN_MODULE_RUNNER_PATH
#define IDHAN_MODULE_RUNNER_PATH "/usr/bin/IDHANModuleRunner"
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

#ifndef IDHAN_MODULE_RUNNER_PATH
#define IDHAN_MODULE_RUNNER_PATH "C:\\ProgramData\\IDHAN\\IDHANModuleRunner.exe"
#endif

#else
#error "No paths supplied for finding IDHAN info. Unsupported OS"
#endif

namespace idhan
{

//! Directory containing the running executable, resolved via /proc/self/exe.
std::filesystem::path getExecutableDir();

std::vector< std::filesystem::path > getModulePaths();

//! Path to the IDHANModuleRunner executable, which hosts one module library per process.
std::filesystem::path getModuleRunnerPath();

std::vector< std::filesystem::path > getMimeParserPaths();
std::filesystem::path getStaticPath();
std::filesystem::path getThumbnailsPath();

//! Square thumbnail edge lengths (px) the server is permitted to write to its on-disk cache. Requests
//! for other sizes are still generated and served, just never cached. Config: `[thumbnails] cacheable_sizes`.
//! Read live per call so an operator can change it without restarting; only hit on a cache miss.
std::vector< std::size_t > getCacheableThumbnailSizes();

//! Whether the on-disk thumbnail cache is used at all. Config: `[thumbnails] cache`, default true.
bool getThumbnailCachingEnabled();

//! Whether to empty the thumbnail cache directory during startup. Config: `[thumbnails] purge_on_boot`.
/** Applies at startup only. Once the server is up, nothing purges the cache again on its own. */
bool getPurgeThumbnailsOnBoot();

//! Directory scanned for WebUI plugin bundles. Each `<dir>/manifest.json` describes one plugin.
//! Defaults to `<static>/plugins` so the existing static file router serves the bundles at `/plugins/...`
//! with no extra routing. Configurable via `[plugins] path`; an override must still be reachable under
//! the `/plugins` URL (i.e. live under the static root) for the browser to fetch the bundle. Cached
//! after first resolution.
std::filesystem::path getPluginsPath();

} // namespace idhan
