

if (NOT TARGET spdlog)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(SPDLOG_USE_STD_FORMAT ON)

	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/spdlog ${CMAKE_CURRENT_BINARY_DIR}/spdlog)
endif ()