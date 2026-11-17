

if (BUILD_IDHAN_DOCS)
	find_package(Doxygen)

	if (doxygen_FOUND)

		set(DOXYGEN_OUT ${CMAKE_CURRENT_SOURCE_DIR}/out)

		add_custom_target(GenerateDoxygenDocs DEPENDS ${DOXYGEN_OUT})

		add_custom_command(
				OUTPUT ${DOXYGEN_OUT}
				COMMAND ${DOXYGEN_EXECUTABLE}
				WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
				COMMENT "Building doxygen docs"
		)

		add_dependencies(IDHANServer GenerateDoxygenDocs)

	endif (doxygen_FOUND)
endif ()
