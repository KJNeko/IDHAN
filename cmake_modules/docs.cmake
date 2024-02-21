option(BUILD_DOC "Build documentation" ON)

if (DEFINED BUILD_DOCS AND BUILD_DOCS)

	find_package(Doxygen)
	if (DOXYGEN_FOUND)
		set(DOXYGEN_IN ${CMAKE_SOURCE_DIR}/Doxyfile)
		set(DOXYGEN_OUT ${CMAKE_BINARY_DIR}/Doxyfile)

		configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

		add_custom_target(doc_doxygen ALL
				COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
				WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
				COMMENT "Generating API documentation with Doxygen"
				VERBATIM
		)
	else ()
		message("Doxygen need to be installed to generate the doxygen documentation")
	endif ()

endif ()