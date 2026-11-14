set(API_DOCS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/apidocs.html")
set(API_DOCS_OUTPUT "${CMAKE_BINARY_DIR}/bin/docs/apidocs.html")

add_custom_command(
		OUTPUT ${API_DOCS_OUTPUT}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/docs"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${API_DOCS_SOURCE} ${API_DOCS_OUTPUT}
		DEPENDS ${API_DOCS_SOURCE}
		COMMENT "Copying API reference documentation"
		VERBATIM
)

add_custom_target(IDHANAPIDocs DEPENDS ${API_DOCS_OUTPUT})
add_dependencies(IDHANServer IDHANAPIDocs)

set(CONFIG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/config-example.toml")
set(CONFIG_OUTPUT "${CMAKE_BINARY_DIR}/bin/config.toml")

add_custom_command(
		OUTPUT ${CONFIG_OUTPUT}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SOURCE} ${CONFIG_OUTPUT}
		DEPENDS ${CONFIG_SOURCE}
		COMMENT "Copying example.toml to binary directory"
		VERBATIM
)

add_custom_target(IDHANConfig DEPENDS ${CONFIG_OUTPUT})
add_dependencies(IDHANServer IDHANConfig)

