include_guard(GLOBAL)

option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)
option(BUILD_IDHAN_WEB "Builds the React WebUI for IDHAN server" ON)
option(BUILD_IDHAN_SERVER "Builds the server" ON)
option(BUILD_IDHAN_DOWNLOADER "Builds the downloader runtime" ON)

option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" OFF)

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" ON)
option(BUILD_IDHAN_HTTP "Builds the proxygen backed HTTP layer" ON)
option(IDHAN_DISABLE_API_AUTH "Disable the API access key checks" OFF)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(IDHAN_TRACE_DEFAULT OFF)
else ()
	set(IDHAN_TRACE_DEFAULT ON)
endif ()

option(IDHAN_ENABLE_TRACE "Enable trace log level" ${IDHAN_TRACE_DEFAULT})

option(BUILD_IDHAN_TOOLS "Builds all tools" ON)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)
option(BUILD_IDHAN_DOWNLOADER_TESTER "Builds the downloader script testing program" ON)

function(idhan_require_option DEPENDENT DEPENDENCY)
	if (${DEPENDENT} AND NOT ${DEPENDENCY})
		get_property(DEPENDENCY_HELP CACHE ${DEPENDENCY} PROPERTY HELPSTRING)
		message(STATUS "${DEPENDENT} requires ${DEPENDENCY}; enabling ${DEPENDENCY}")
		set(${DEPENDENCY} ON CACHE BOOL "${DEPENDENCY_HELP}" FORCE)
		set(${DEPENDENCY} ON PARENT_SCOPE)
		set(IDHAN_OPTIONS_CHANGED TRUE PARENT_SCOPE)
	endif ()
endfunction()

# Resolve the component graph to a fixed point; tests/docs may enable the server,
# which in turn enables HTTP and the downloader.
set(IDHAN_OPTIONS_CHANGED TRUE)
while (IDHAN_OPTIONS_CHANGED)
	set(IDHAN_OPTIONS_CHANGED FALSE)
	idhan_require_option(BUILD_IDHAN_TESTS BUILD_IDHAN_SERVER)
	idhan_require_option(BUILD_IDHAN_DOCS BUILD_IDHAN_SERVER)
	idhan_require_option(BUILD_IDHAN_SERVER BUILD_IDHAN_HTTP)
	idhan_require_option(BUILD_IDHAN_SERVER BUILD_IDHAN_DOWNLOADER)
	idhan_require_option(BUILD_IDHAN_DOWNLOADER_TESTER BUILD_IDHAN_DOWNLOADER)
	idhan_require_option(BUILD_HYDRUS_IMPORTER BUILD_IDHAN_CLIENT)
	idhan_require_option(BUILD_IDHAN_TOOLS BUILD_IDHAN_CLIENT)
endwhile ()
unset(IDHAN_OPTIONS_CHANGED)

option(IDHAN_USE_JEMALLOC "Link the server against jemalloc instead of the system allocator" ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(IDHAN_HARDEN_DEFAULT OFF)
else ()
	set(IDHAN_HARDEN_DEFAULT ON)
endif ()

option(IDHAN_HARDEN "Block same-uid ptrace of the server and disable worker core dumps" ${IDHAN_HARDEN_DEFAULT})

option(IDHAN_WEB_AUTO_REBUILD "Rebuild the WebUI whenever IDHANServer is built" ON)
option(IDHAN_PACKAGE_HYDRUS_IMPORTER "Bundle the Hydrus Importer runtime for releases" OFF)
option(BUILD_PTR_CORE_TESTS "Build the standalone PTR file storage tests" OFF)
