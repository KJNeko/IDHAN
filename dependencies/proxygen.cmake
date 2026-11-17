include_guard(GLOBAL)

add_library(IDHANProxygen INTERFACE)
target_compile_definitions(IDHANProxygen INTERFACE nsel_CONFIG_SELECT_EXPECTED=1)
target_link_libraries(
		IDHANProxygen
		INTERFACE proxygen::proxygen_http_coro_server_coro_httpserver
		proxygen::proxygen_http_coro_coro_reader
		proxygen::proxygen_coro
		proxygen::proxygen_http_coro_server_handlers_expect_continue_wrapper_handler
		proxygen::proxygen_http_coro_filters_compression_filter_factory
		proxygen::proxygen_utils_time)

find_package(proxygen CONFIG QUIET)

if (TARGET proxygen::proxygen_http_coro_server_coro_httpserver)
	return()
endif ()

set(PROXYGEN_SOURCE_DIR ${IDHAN_DEPENDENCIES}/proxygen)

if (NOT EXISTS ${PROXYGEN_SOURCE_DIR}/CMakeLists.txt)
	message(FATAL_ERROR "proxygen is missing; initialize the dependencies/proxygen submodule")
endif ()

option(IDHAN_FETCH_PROXYGEN_DEPENDENCIES "Fetch folly, fizz, wangle and mvfst when they are not installed" ON)

set(PROXYGEN_MISSING_TOOLS "")

find_program(PROXYGEN_GPERF gperf)

if (NOT PROXYGEN_GPERF)
	list(APPEND PROXYGEN_MISSING_TOOLS gperf)
endif ()

find_program(PROXYGEN_PYTHON python3)

if (NOT PROXYGEN_PYTHON)
	list(APPEND PROXYGEN_MISSING_TOOLS python3)
endif ()

if (PROXYGEN_MISSING_TOOLS)
	list(JOIN PROXYGEN_MISSING_TOOLS ", " PROXYGEN_MISSING_TOOL_NAMES)
	message(FATAL_ERROR "proxygen is missing dependencies: ${PROXYGEN_MISSING_TOOL_NAMES}")
endif ()

# proxygen's hash bot sometimes drops a pin from a tagged release. v2026.11.09.00 has no folly pin; this is the last
# one proxygen recorded before it was removed on 2026-11-07.
set(PROXYGEN_FALLBACK_FOLLY_REVISION "b6e123b9428174abb6a075ef9fd92886f44d2b63")

function(proxygen_pinned_revision OUT_VAR OWNER NAME)
	set(REVISION_FILE ${PROXYGEN_SOURCE_DIR}/build/deps/github_hashes/${OWNER}/${NAME}-rev.txt)

	string(TOUPPER ${NAME} NAME_UPPER)
	set(FALLBACK_REVISION ${PROXYGEN_FALLBACK_${NAME_UPPER}_REVISION})

	if (NOT EXISTS ${REVISION_FILE} AND FALLBACK_REVISION)
		message(STATUS "proxygen: ${REVISION_FILE} is missing; using ${NAME} ${FALLBACK_REVISION}")
		set(${OUT_VAR} ${FALLBACK_REVISION} PARENT_SCOPE)
		return()
	endif ()

	if (NOT EXISTS ${REVISION_FILE})
		message(FATAL_ERROR "proxygen no longer pins ${NAME}; ${REVISION_FILE} is missing")
	endif ()

	file(READ ${REVISION_FILE} REVISION_CONTENTS)

	if (NOT REVISION_CONTENTS MATCHES "commit +([0-9a-f]+)")
		message(FATAL_ERROR "proxygen's ${NAME} pin is unreadable: ${REVISION_FILE}")
	endif ()

	set(${OUT_VAR} ${CMAKE_MATCH_1} PARENT_SCOPE)
endfunction()

proxygen_pinned_revision(FOLLY_REVISION facebook folly)
proxygen_pinned_revision(FIZZ_REVISION facebookincubator fizz)
proxygen_pinned_revision(WANGLE_REVISION facebook wangle)
proxygen_pinned_revision(MVFST_REVISION facebook mvfst)

set(FOLLY_GIT_REPOSITORY "https://github.com/facebook/folly.git")
set(FIZZ_GIT_REPOSITORY "https://github.com/facebookincubator/fizz.git")
set(WANGLE_GIT_REPOSITORY "https://github.com/facebook/wangle.git")
set(MVFST_GIT_REPOSITORY "https://github.com/facebook/mvfst.git")

set(FIZZ_SOURCE_SUBDIR fizz)
set(WANGLE_SOURCE_SUBDIR wangle)

set(PROXYGEN_MISSING "")

foreach (PROXYGEN_DEPENDENCY folly fizz wangle mvfst)
	find_package(${PROXYGEN_DEPENDENCY} CONFIG QUIET)

	if (NOT ${PROXYGEN_DEPENDENCY}_FOUND)
		list(APPEND PROXYGEN_MISSING ${PROXYGEN_DEPENDENCY})
	endif ()
endforeach ()

if (PROXYGEN_MISSING AND NOT IDHAN_FETCH_PROXYGEN_DEPENDENCIES)
	list(JOIN PROXYGEN_MISSING ", " PROXYGEN_MISSING_NAMES)
	message(FATAL_ERROR "proxygen is missing dependencies: ${PROXYGEN_MISSING_NAMES}")
endif ()

set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_compile_definitions(nsel_CONFIG_SELECT_EXPECTED=1)

if (PROXYGEN_MISSING)
	include(FetchContent)

	list(JOIN PROXYGEN_MISSING ", " PROXYGEN_MISSING_NAMES)
	message(STATUS "proxygen: fetching ${PROXYGEN_MISSING_NAMES}")

	foreach (PROXYGEN_DEPENDENCY ${PROXYGEN_MISSING})
		string(TOUPPER ${PROXYGEN_DEPENDENCY} PROXYGEN_DEPENDENCY_UPPER)

		set(PROXYGEN_FETCH_ARGS "")

		if (${PROXYGEN_DEPENDENCY_UPPER}_SOURCE_SUBDIR)
			list(APPEND PROXYGEN_FETCH_ARGS SOURCE_SUBDIR ${${PROXYGEN_DEPENDENCY_UPPER}_SOURCE_SUBDIR})
		endif ()

		FetchContent_Declare(
				${PROXYGEN_DEPENDENCY}
				GIT_REPOSITORY ${${PROXYGEN_DEPENDENCY_UPPER}_GIT_REPOSITORY}
				GIT_TAG ${${PROXYGEN_DEPENDENCY_UPPER}_REVISION}
				${PROXYGEN_FETCH_ARGS}
				EXCLUDE_FROM_ALL
				SYSTEM)

		FetchContent_MakeAvailable(${PROXYGEN_DEPENDENCY})
	endforeach ()
endif ()

add_subdirectory(${PROXYGEN_SOURCE_DIR} ${CMAKE_BINARY_DIR}/proxygen EXCLUDE_FROM_ALL SYSTEM)
