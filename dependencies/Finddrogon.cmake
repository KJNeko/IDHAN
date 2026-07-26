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

	# Tracy fiber tracing for drogon::Task WITHOUT modifying the drogon submodule: generate a patched
	# COPY of coroutine.h in the build tree and shadow the original by placing the copy's include dir
	# ahead of drogon's -- for both drogon's own build and its consumers. The submodule stays pristine;
	# the patch lives at dependencies/patches/drogon-tracy-fibers.patch and is tracked and reviewable.
	if (IDHAN_ENABLE_TRACY AND TARGET Tracy::TracyClient)
		find_package(Git REQUIRED)

		set(_drogon_pristine_hdr ${CMAKE_SOURCE_DIR}/dependencies/drogon/lib/inc/drogon/utils/coroutine.h)
		set(_drogon_tracy_patch ${CMAKE_SOURCE_DIR}/dependencies/patches/drogon-tracy-fibers.patch)
		set(_drogon_shim_dir ${CMAKE_BINARY_DIR}/drogon-tracy-shim)
		set(_drogon_shim_hdr ${_drogon_shim_dir}/lib/inc/drogon/utils/coroutine.h)

		# Re-copy the pristine header on every configure so the patch always applies to a clean base
		# (it can never double-apply), then patch the copy in place.
		configure_file(${_drogon_pristine_hdr} ${_drogon_shim_hdr} COPYONLY)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} apply --unsafe-paths -p1 --directory=${_drogon_shim_dir} ${_drogon_tracy_patch}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE _drogon_patch_result
			ERROR_VARIABLE _drogon_patch_error)
		if (NOT _drogon_patch_result EQUAL 0)
			message(FATAL_ERROR
				"Failed to apply ${_drogon_tracy_patch} to the drogon coroutine.h shim. The drogon "
				"submodule was likely bumped and the patch no longer matches its coroutine.h. Update "
				"dependencies/patches/drogon-tracy-fibers.patch to match and reconfigure.\n"
				"git apply error:\n${_drogon_patch_error}")
		endif ()

		# Shadow drogon's coroutine.h with the patched copy: BEFORE + PUBLIC puts the shim ahead of
		# drogon's real include dir for drogon's own translation units and for every consumer, so the
		# promise_type is identical everywhere (no ODR skew). The shim dir holds only coroutine.h, so
		# all other drogon headers still resolve from the pristine include dir behind it.
		target_include_directories(drogon BEFORE PUBLIC $<BUILD_INTERFACE:${_drogon_shim_dir}/lib/inc>)

		target_link_libraries(drogon PUBLIC
			$<BUILD_INTERFACE:Tracy::TracyClient>
			$<BUILD_INTERFACE:idhan_tracy_coro>)
	endif ()
endif()

set(drogon_FOUND TRUE)
