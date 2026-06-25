
set(SPDLOG_INSTALL ON)
set(SPDLOG_FMT_EXTERNAL ON)

set(FMT_INSTALL ON)

if (NOT TARGET fmt::fmt)
	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/fmt ${CMAKE_CURRENT_BINARY_DIR}/fmt)
endif ()

if (NOT TARGET spdlog)
	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/spdlog ${CMAKE_CURRENT_BINARY_DIR}/spdlog)
endif ()