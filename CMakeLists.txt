# /CMakeLists.txt

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_STATIC_LIBS_ONLY ON)
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

cmake_minimum_required(VERSION 3.28)
project(IDHAN LANGUAGES CXX)

set(IDHAN_DEPENDENCIES ${CMAKE_CURRENT_SOURCE_DIR}/dependencies)

enable_testing()

# Includes fgl_cmake_modules (defines AddFGLLibrary/AddFGLExecutable/AddFGLModule
# and the fgl::flags target). This configures the FGL build helpers on include;
# there is no separate PreSetup()/PostSetup() step.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libFGL)

# Find-modules (Finddrogon.cmake, postgres.cmake, ...) and config packages live
# under dependencies/.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/dependencies")

option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)
option(BUILD_IDHAN_WEB "Builds the React WebUI for IDHAN server" ON)
option(BUILD_IDHAN_SERVER "Builds the server" ON)

option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" OFF)

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" ON)
option(IDHAN_DISABLE_API_AUTH "Disable the API access key checks" OFF)

option(IDHAN_ENABLE_TRACE "Enable trace log level" ON)

option(IDHAN_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)

option(BUILD_IDHAN_TOOLS "Builds all tools" ON)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)

if (BUILD_HYDRUS_IMPORTER EQUAL 1)
	set(BUILD_IDHAN_CLIENT ON)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(IDHAN_ENABLE_TRACE OFF)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (IDHAN_ENABLE_TRACY)
	# Tracy client options must be set before its add_subdirectory so they bake into the target.
	set(TRACY_ENABLE ON CACHE BOOL "" FORCE)      # defines TRACY_ENABLE PUBLIC on Tracy::TracyClient
	set(TRACY_FIBERS ON CACHE BOOL "" FORCE)      # enables fiber (coroutine) support
	set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)   # only collect once a profiler connects
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy)
	# Dependency-level shared coroutine-fiber helpers, included by both IDHAN promises and the
	# drogon patch. Exposed as an INTERFACE target so its include dir propagates cleanly.
	add_library(idhan_tracy_coro INTERFACE)
	target_include_directories(idhan_tracy_coro INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy-coro/include)
	target_link_libraries(idhan_tracy_coro INTERFACE Tracy::TracyClient)
endif ()

#IDHANServer/IDHANClient
include(postgres)
include(toml)

find_package(spdlog REQUIRED)
find_package(Jsoncpp REQUIRED)

add_subdirectory(IDHAN)
if (BUILD_IDHAN_SERVER)
	add_subdirectory(IDHANMigration)
	add_subdirectory(IDHANModules)
	add_subdirectory(IDHANServer)
endif ()
if (BUILD_IDHAN_CLIENT)
	add_subdirectory(IDHANClient)
endif ()

if (BUILD_HYDRUS_IMPORTER OR BUILD_IDHAN_TOOLS)
	#HydrusImporter
	include(sqlite3)
	add_subdirectory(tools/HydrusImporter)
endif ()

if (BUILD_IDHAN_DOCS)
	add_subdirectory(docs)
endif ()

if (BUILD_IDHAN_WEB)
	add_subdirectory(IDHANWeb)
endif ()

if (BUILD_IDHAN_TESTS)
	#IDHANTests
	add_subdirectory(tests)
endif ()