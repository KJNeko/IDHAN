# Keep warnings enabled for IDHAN and our libFGL submodule. External headers
# are system includes for consumers; external translation units are compiled quietly.
function(idhan_configure_external_warnings DIRECTORY)
	get_directory_property(TARGETS DIRECTORY "${DIRECTORY}" BUILDSYSTEM_TARGETS)
	foreach (TARGET_NAME IN LISTS TARGETS)
		get_target_property(TARGET_SOURCE_DIR ${TARGET_NAME} SOURCE_DIR)
		set(EXTERNAL_ROOT "${CMAKE_SOURCE_DIR}/dependencies")
		cmake_path(IS_PREFIX EXTERNAL_ROOT "${TARGET_SOURCE_DIR}" NORMALIZE IS_EXTERNAL)
		set(FGL_ROOT "${CMAKE_SOURCE_DIR}/dependencies/libFGL")
		cmake_path(IS_PREFIX FGL_ROOT "${TARGET_SOURCE_DIR}" NORMALIZE IS_FGL)
		set(FETCH_ROOT "${CMAKE_BINARY_DIR}/_deps")
		cmake_path(IS_PREFIX FETCH_ROOT "${TARGET_SOURCE_DIR}" NORMALIZE IS_FETCHED)
		set(THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/3rd-party")
		cmake_path(IS_PREFIX THIRD_PARTY_ROOT "${TARGET_SOURCE_DIR}" NORMALIZE IS_THIRD_PARTY)

		# These targets are declared in our directories but contain only external sources.
		if ((IS_EXTERNAL AND NOT IS_FGL) OR IS_FETCHED OR IS_THIRD_PARTY
				OR TARGET_NAME STREQUAL "IDHANQuickJS" OR TARGET_NAME STREQUAL "chardet")
			set_property(TARGET ${TARGET_NAME} PROPERTY SYSTEM TRUE)
			get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
			if (NOT TARGET_TYPE STREQUAL "INTERFACE_LIBRARY" AND NOT TARGET_TYPE STREQUAL "UTILITY")
				target_compile_options(${TARGET_NAME} PRIVATE
						"$<$<OR:$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-w>"
						"$<$<OR:$<COMPILE_LANG_AND_ID:C,MSVC>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/w>")
			endif ()
		endif ()
	endforeach ()

	get_directory_property(SUBDIRECTORIES DIRECTORY "${DIRECTORY}" SUBDIRECTORIES)
	foreach (SUBDIRECTORY IN LISTS SUBDIRECTORIES)
		idhan_configure_external_warnings("${SUBDIRECTORY}")
	endforeach ()
endfunction()
