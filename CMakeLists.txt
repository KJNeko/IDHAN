# /CMakeLists.txt

cmake_minimum_required(VERSION 3.25.0)
project(IDHAN LANGUAGES CXX)

enable_testing()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fgl_cmake_modules)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

PreSetup()


option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" ON)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)

#IDHANServer/IDHANClient
include(spdlog)
include(postgres)
include(drogon)
include(qt6)
add_subdirectory(IDHAN)
add_subdirectory(IDHANServer)
add_subdirectory(IDHANClient)

if (BUILD_IDHAN_TESTS)
	#IDHANTests
	include(Catch)
	add_subdirectory(tests)
endif ()


if (BUILD_HYDRUS_IMPORTER)
	#HydrusImporter
	include(sqlite3)
	add_subdirectory(HydrusImporter)
endif ()

PostSetup()