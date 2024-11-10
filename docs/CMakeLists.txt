

if (BUILD_IDHAN_DOCS)
	find_package(Doxygen)

	set(API_YAML ${CMAKE_CURRENT_SOURCE_DIR}/api.yaml)
	set(API_YAML_OUT ${CMAKE_BINARY_DIR}/bin/pages/api.yaml)
	set(DOXYGEN_OUT ${CMAKE_CURRENT_SOURCE_DIR}/out)

	add_custom_target(IDHANDocs DEPENDS ${API_YAML_OUT} ${DOXYGEN_OUT})

	add_custom_command(
			OUTPUT ${DOXYGEN_OUT}
			COMMAND ${DOXYGEN_EXECUTABLE}
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
			COMMENT "Building doxygen docs"
	)

	add_custom_command(
			OUTPUT ${API_YAML_OUT}
			DEPENDS ${API_YAML}
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${API_YAML} ${API_YAML_OUT}
			COMMENT "Copying API docs"
	)

	add_dependencies(IDHANServer IDHANDocs)
endif ()
