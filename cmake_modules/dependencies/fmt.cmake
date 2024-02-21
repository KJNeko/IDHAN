if (NOT HAS_STD_FORMAT AND NOT ATLAS_IGNORE_STD_FORMAT EQUAL 1)
	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/fmt)
	set(ATLAS_LINK_FMT 1)
	option(SPDLOG_FMT_EXTERNAL "" ON)
else ()
	option(SPDLOG_FMT_EXTERNAL "" OFF)
	#dummy lib to link to
	set(ATLAS_LINK_FMT 0)
endif ()
