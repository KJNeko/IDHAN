include_guard(GLOBAL)

set(SIMDJSON_SOURCE_DIR ${IDHAN_DEPENDENCIES}/simdjson)
if (NOT EXISTS ${SIMDJSON_SOURCE_DIR}/CMakeLists.txt)
	message(FATAL_ERROR "simdjson is missing; initialize the dependencies/simdjson submodule")
endif ()

set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
set(SIMDJSON_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${SIMDJSON_SOURCE_DIR} ${CMAKE_BINARY_DIR}/simdjson EXCLUDE_FROM_ALL SYSTEM)
