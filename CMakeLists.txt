cmake_minimum_required(VERSION 3.28)

project(IDHAN LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectSettings)
include(Options)
include(Dependencies)

# CTest must be enabled at the root so all component suites are discoverable.
if (BUILD_IDHAN_TESTS OR BUILD_PTR_CORE_TESTS)
	enable_testing()
endif ()

add_subdirectory(IDHAN)

if (BUILD_IDHAN_DOWNLOADER)
	add_subdirectory(IDHANDownloader)
endif ()

if (BUILD_IDHAN_SERVER OR BUILD_IDHAN_HTTP)
	add_subdirectory(IDHANHttp)
endif ()

if (BUILD_IDHAN_SERVER)
	add_subdirectory(IDHANDb)
	add_subdirectory(IDHANMigration)
	add_subdirectory(IDHANModules)
	add_subdirectory(IDHANServer)
endif ()

if (BUILD_IDHAN_CLIENT)
	add_subdirectory(IDHANClient)
endif ()

if (BUILD_HYDRUS_IMPORTER OR BUILD_IDHAN_TOOLS)
	add_subdirectory(tools/HydrusImporter)
elseif (BUILD_PTR_CORE_TESTS)
	add_subdirectory(tools/HydrusImporter/ptr-core)
endif ()

if (BUILD_IDHAN_DOCS)
	add_subdirectory(docs)
endif ()

if (BUILD_IDHAN_WEB)
	add_subdirectory(IDHANWeb)
endif ()

if (BUILD_IDHAN_TESTS)
	add_subdirectory(tests)
endif ()

include(ExternalWarnings)
idhan_configure_external_warnings("${CMAKE_CURRENT_SOURCE_DIR}")
