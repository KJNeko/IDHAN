

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

	set(API_YAML ${CMAKE_CURRENT_SOURCE_DIR}/api)
	set(API_YAML_OUT ${CMAKE_BINARY_DIR}/bin/static/api)
	file(GLOB_RECURSE YAML_FILES CONFIGURE_DEPENDS
			"${CMAKE_CURRENT_SOURCE_DIR}/api/*.yaml"
	)

	add_custom_command(
			OUTPUT ${API_YAML_OUT}
			DEPENDS ${YAML_FILES}
			COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${API_YAML} ${API_YAML_OUT}
			COMMENT "Copying API docs"
	)


	add_custom_target(GenerateAPIYaml DEPENDS ${API_YAML_OUT})

	add_dependencies(IDHANServer GenerateAPIYaml)
endif ()
