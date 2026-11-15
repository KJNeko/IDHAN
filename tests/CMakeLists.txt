# API tests run independently with pytest; see api/README.md.
add_subdirectory(shared)

if (BUILD_IDHAN_DOWNLOADER)
	add_subdirectory(downloader)
endif ()
