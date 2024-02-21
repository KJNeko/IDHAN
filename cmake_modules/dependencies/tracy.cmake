

string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_UPPER_BUILD_TYPE)

message("-- Cmake upper: ${CMAKE_UPPER_BUILD_TYPE}")

if (CMAKE_UPPER_BUILD_TYPE STREQUAL "DEBUG")
	set(TRACY_ENABLE ON)
	set(TRACY_ON_DEMAND ON)
endif ()

add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/tracy)

