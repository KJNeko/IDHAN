project(IDHANServer LANGUAGES CXX C)

AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(IDHANServer PRIVATE ${MIGRATION_SOURCE})

# Gui is needed for QImage for whatever reason
find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)
find_package(spdlog REQUIRED)
find_package(drogon REQUIRED)

if (spdlog_FOUND)
else ()
	message(SEND_ERROR "Spdlog not found")
endif ()

target_link_libraries(IDHANServer PUBLIC spdlog drogon)
target_link_libraries(IDHANServer PRIVATE pqxx Qt6::Core Qt6::Multimedia tomlplusplus::tomlplusplus uring)
target_link_libraries(IDHANServer PRIVATE IDHAN)
target_link_libraries(IDHANServer PUBLIC IDHANModules IDHANMigration)
target_compile_definitions(IDHANServer PUBLIC IDHAN_USE_STD_FORMAT)

# Copy page info to the output directory

file(GLOB_RECURSE PAGE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/static/*")
set(PAGE_OUTPUTS "")

foreach (PAGE ${PAGE_FILES})

	file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src ${PAGE})

	set(OUT_PATH ${CMAKE_BINARY_DIR}/bin/${REL_PATH})

	add_custom_command(
			OUTPUT ${OUT_PATH}
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PAGE} ${OUT_PATH}
			DEPENDS ${PAGE}
			COMMENT "Copying ${PAGE} to ${OUT_PATH}"
	)

	list(APPEND PAGE_OUTPUTS ${OUT_PATH})

endforeach ()

add_custom_target(IDHANPages DEPENDS ${PAGE_OUTPUTS})
add_dependencies(IDHANServer IDHANPages)

#add_dependencies(IDHANServer IDHANModules)

# Copy mime identifiers to the output dir
file(GLOB_RECURSE PARSER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/mime/parsers/*")
set(PARSER_OUTPUTS "")

foreach (PARSER ${PARSER_FILES})
	file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/mime/parsers ${PARSER})

	set(OUT_PATH ${CMAKE_BINARY_DIR}/bin/mime/${REL_PATH})

	add_custom_command(
			OUTPUT ${OUT_PATH}
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PARSER} ${OUT_PATH}
			DEPENDS ${PARSER}
			COMMENT "Copying ${PARSER} to ${OUT_PATH}"
	)

	list(APPEND PARSER_OUTPUTS ${OUT_PATH})
endforeach ()

add_custom_target(IDHANMimeParsers DEPENDS ${PARSER_OUTPUTS})
add_dependencies(IDHANServer IDHANMimeParsers)

set(CONFIG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/config-example.toml")
set(CONFIG_OUTPUT "${CMAKE_BINARY_DIR}/bin/config.toml")

add_custom_command(
		OUTPUT ${CONFIG_OUTPUT}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONFIG_SOURCE} ${CONFIG_OUTPUT}
		DEPENDS ${CONFIG_SOURCE}
		COMMENT "Copying example.toml to binary directory"
)

add_custom_target(IDHANConfig DEPENDS ${CONFIG_OUTPUT})
add_dependencies(IDHANServer IDHANConfig)

add_dependencies(IDHANServer IDHANPremadeModules)

if (DEFINED IDHAN_WEBUI AND IDHAN_WEBUI EQUAL ON)
	add_dependencies(IDHANServer IDHANWebUI)
endif ()