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

# Builds the Tracy profiler GUI from the vendored source as the `tracy-profiler` target (so it can be
# built from an IDE like CLion). Independent of IDHAN_ENABLE_TRACY: the GUI connects to any Tracy
# client, and building it pins the GUI to the same v0.11.1 protocol as our instrumented client.
option(IDHAN_BUILD_TRACY_PROFILER "Build the Tracy profiler GUI (tracy-profiler target)" OFF)

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
	# Dependency-level shared coroutine-fiber helpers, included by both IDHAN promises and the drogon
	# coroutine shim. A STATIC lib (not INTERFACE) so it can compile CoroFiber.cpp — the ONE TU that
	# includes <tracy/Tracy.hpp> and defines the fiberEnter/fiberLeave shims. This keeps the public
	# header free of Tracy's heavy client headers (which otherwise collide with system/Qt macros in
	# every TU that transitively includes the shimmed drogon coroutine.h). Tracy is linked PUBLIC so
	# consumers get TRACY_ENABLE + the include dir.
	add_library(idhan_tracy_coro STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy-coro/src/CoroFiber.cpp)
	target_include_directories(idhan_tracy_coro PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy-coro/include)
	target_link_libraries(idhan_tracy_coro PUBLIC Tracy::TracyClient)
endif ()

if (IDHAN_BUILD_TRACY_PROFILER)
	# Prefer system libraries over CPM network downloads (capstone defaults to force-download upstream).
	# The default backend on Linux is native Wayland (needs wayland-scanner + wayland-* dev libs); for an
	# X11 session, configure the profiler subdir with -DLEGACY=ON (uses GLFW, which is also present).
	set(DOWNLOAD_CAPSTONE OFF CACHE BOOL "" FORCE)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/tracy/profiler)
	if (TARGET tracy-profiler)
		# Tracy 0.11.1's vendored server/tracy_robin_hood.h uses uint*_t without including <cstdint>;
		# newer libstdc++ (GCC 13+) no longer pulls it in transitively. Force-include it — but only for
		# C++ sources (the target also has C files, for which <cstdint> does not exist).
		target_compile_options(tracy-profiler PRIVATE
			$<$<COMPILE_LANGUAGE:CXX>:-include>
			$<$<COMPILE_LANGUAGE:CXX>:cstdint>)
	endif ()
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