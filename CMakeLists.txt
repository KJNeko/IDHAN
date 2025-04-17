# /CMakeLists.txt

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_minimum_required(VERSION 3.28)
project(IDHAN LANGUAGES CXX)

enable_testing()

# Includes fgl_cmake_modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libFGL)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/dependencies")

PreSetup()

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" ON)
option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" ON)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)
option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)

if (BUILD_HYDRUS_IMPORTER)
	set(BUILD_IDHAN_CLIENT ON)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#IDHANServer/IDHANClient
include(postgres)
include(toml)

add_subdirectory(IDHAN)
add_subdirectory(IDHANServer)
add_subdirectory(IDHANClient)
add_subdirectory(docs)


if (BUILD_IDHAN_TESTS)
	#IDHANTests
	add_subdirectory(tests)
endif ()


if (BUILD_HYDRUS_IMPORTER)
	#HydrusImporter
	include(sqlite3)
	add_subdirectory(HydrusImporter)
endif ()

PostSetup()