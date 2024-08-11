# /CMakeLists.txt

cmake_minimum_required(VERSION 3.25.0)
project(IDHAN LANGUAGES CXX)

enable_testing()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fgl_cmake_modules)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

PreSetup()

include(Catch)
include(spdlog)
include(postgres)
include(sqlite3)
include(drogon)
include(qt6)

#add_subdirectory(IDHAN)
add_subdirectory(IDHANServer)
add_subdirectory(IDHANClient)
add_subdirectory(tests)

PostSetup()