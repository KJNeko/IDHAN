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
	# ahead of drogon's. The submodule stays pristine; the patch is tracked and reviewable.
	if (IDHAN_ENABLE_TRACY AND TARGET Tracy::TracyClient)
		set(_drogon_coro_src "${CMAKE_SOURCE_DIR}/dependencies/drogon/lib/inc/drogon/utils/coroutine.h")
		set(_drogon_shim_dir "${CMAKE_BINARY_DIR}/drogon-tracy-shim")
		set(_drogon_shim_inc "${_drogon_shim_dir}/lib/inc")
		set(_drogon_shim_hdr "${_drogon_shim_inc}/drogon/utils/coroutine.h")
		set(_drogon_patch "${CMAKE_SOURCE_DIR}/dependencies/patches/drogon-tracy-fibers.patch")

		# Fresh pristine copy each configure, then apply the patch to the COPY (idempotent: the copy
		# always differs from the pristine source, so copy_if_different re-copies before patching).
		file(MAKE_DIRECTORY "${_drogon_shim_inc}/drogon/utils")
		configure_file("${_drogon_coro_src}" "${_drogon_shim_hdr}" COPYONLY)
		find_program(PATCH_EXECUTABLE patch REQUIRED)
		execute_process(
			COMMAND "${PATCH_EXECUTABLE}" -p1 -d "${_drogon_shim_dir}" -i "${_drogon_patch}"
			RESULT_VARIABLE _drogon_patch_result
			OUTPUT_VARIABLE _drogon_patch_output
			ERROR_VARIABLE _drogon_patch_output)
		if (NOT _drogon_patch_result EQUAL 0)
			message(FATAL_ERROR "Failed to apply drogon Tracy shim patch:\n${_drogon_patch_output}")
		endif ()

		# Shadow coroutine.h in BOTH contexts, with the correct system/non-system treatment so the
		# shim wins the include search in each and drogon's own TUs and consumers agree on the
		# Task::promise_type layout (ODR safety):
		#  - drogon's OWN build compiles its headers as normal -I, so the shim must be -I + BEFORE.
		#  - CONSUMERS see drogon's headers as -isystem (add_subdirectory ... SYSTEM), so the shim
		#    must be -isystem + BEFORE, which also keeps IDHAN's strict warnings off drogon's code.
		target_include_directories(drogon BEFORE PRIVATE "${_drogon_shim_inc}")
		# BUILD_INTERFACE-wrapped: a raw build-dir path in the INTERFACE fails drogon's install(EXPORT).
		target_include_directories(drogon SYSTEM BEFORE INTERFACE "$<BUILD_INTERFACE:${_drogon_shim_inc}>")

		# Tracy + the shared fiber header, linked BUILD-only (generator expr) so these non-exported
		# helper targets do not enter drogon's install(EXPORT DrogonTargets) link interface.
		target_link_libraries(drogon PUBLIC
			$<BUILD_INTERFACE:Tracy::TracyClient>
			$<BUILD_INTERFACE:idhan_tracy_coro>)
	endif ()
endif()

set(drogon_FOUND TRUE)
