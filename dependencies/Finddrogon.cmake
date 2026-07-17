if(NOT TARGET drogon)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_MYSQL OFF CACHE BOOL "" FORCE)
    set(BUILD_SQLITE OFF CACHE BOOL "" FORCE)
    set(BUILD_REDIS OFF CACHE BOOL "" FORCE)
    set(BUILD_BROTLI OFF CACHE BOOL "" FORCE)
    set(BUILD_YAML_CONFIG OFF CACHE BOOL "" FORCE)
    set(BUILD_CTL OFF CACHE BOOL "" FORCE)

	# Build drogon (and its trantor sub-dependency) as an external dependency.
	# SYSTEM marks their interface include directories as system headers, so our
	# strict warning set does not fire when we include <drogon/...> / <trantor/...>.
	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/drogon ${CMAKE_BINARY_DIR}/drogon-build SYSTEM)

	# drogon and trantor compile themselves with -Wall -Wextra -Werror. A trailing
	# -w (which overrides those) keeps their own build quiet and, importantly,
	# stops their -Werror from breaking the build on newer compilers.
	if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		foreach (_dep_target IN ITEMS drogon trantor)
			if (TARGET ${_dep_target})
				target_compile_options(${_dep_target} PRIVATE -w)
			endif ()
		endforeach ()
	endif ()
endif()

set(drogon_FOUND TRUE)
