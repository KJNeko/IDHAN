# /CMakeLists.txt

cmake_minimum_required(VERSION 3.25.0)
project(Game LANGUAGES CXX)

enable_testing()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MESSAGE_LOG_LEVEL DEBUG CACHE STRING "CMake messaging level")

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/data)

#Enable cmake_modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")
include(common)

message("-- CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
string(TOUPPER ${CMAKE_BUILD_TYPE} UPPER_BUILD_TYPE)

PlatformPreSetup()
CompilerPreSetup()
message("-- FGL_FLAGS: ${FGL_FLAGS}")

include(dependencies/catch2)
include(dependencies/openssl)
include(dependencies/asio)
include(dependencies/spdlog)

add_subdirectory(src)
add_subdirectory(tests)

SetVersionInfo()
CompilerPostSetup()