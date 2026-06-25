
set(SPDLOG_INSTALL ON)
set(SPDLOG_FMT_EXTERNAL ON)

if (NOT TARGET spdlog)
	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/spdlog ${CMAKE_CURRENT_BINARY_DIR}/spdlog)
endif ()