
set(BUILD_MYSQL OFF)
set(BUILD_SQLITE OFF)
set(BUILD_REDIS OFF)
set(BUILD_ORM ON)
set(BUILD_EXAMPLES OFF)
set(BUILD_CTL OFF)

set(USE_MYSQL OFF)
set(USE_SQLITE3 OFF)

set(USE_SPDLOG ON)

set(BUILD_TESTING OFF)

set(USE_SUBMODULE OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/drogon ${CMAKE_CURRENT_BINARY_DIR}/drogon)
set(drogon_FOUND TRUE)


# In order to silence a lot of warnings from our flags, we need to mark all drogon's includes as system headers
get_target_property(DROGON_INCLUDE_DIRS drogon INTERFACE_INCLUDE_DIRECTORIES)
if (DROGON_INCLUDE_DIRS)
	target_include_directories(drogon SYSTEM INTERFACE ${DROGON_INCLUDE_DIRS})
endif ()