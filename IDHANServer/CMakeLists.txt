
AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(MIGRATION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/doMigration.cpp")
file(REMOVE ${MIGRATION_SOURCE})

target_sources(IDHANServer PRIVATE ${MIGRATION_SOURCE})

set(MIGRATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/db/setup/migration)

file(GLOB_RECURSE MIGRATION_SQLS CONFIGURE_DEPENDS ${MIGRATION_DIR}/*.sql)

add_custom_command(
		OUTPUT ${MIGRATION_SOURCE}
		DEPENDS ${MIGRATION_SQLS}
		COMMAND ${CMAKE_COMMAND} -DMIGRATION_DIR=${MIGRATION_DIR} -DOUT=${MIGRATION_SOURCE} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateMigrations.cmake"
		COMMENT "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateMigrations.cmake: Generating doMigration.cpp")


# Gui is needed for QImage for whatever reason
find_package(Qt6 REQUIRED COMPONENTS Core Multimedia)
find_package(spdlog REQUIRED)
find_package(drogon REQUIRED)

if (spdlog_FOUND)
else ()
	message(SEND_ERROR "Spdlog not found")
endif ()

target_link_libraries(IDHANServer PUBLIC spdlog drogon)
target_link_libraries(IDHANServer PRIVATE pqxx Qt6::Core Qt6::Multimedia tomlplusplus::tomlplusplus)
target_link_libraries(IDHANServer PRIVATE IDHAN)
target_link_libraries(IDHANServer PUBLIC IDHANModules)
target_compile_definitions(IDHANServer PUBLIC IDHAN_USE_STD_FORMAT)

if (DEFINED ALLOW_TABLE_DESTRUCTION AND ALLOW_TABLE_DESTRUCTION)
	target_compile_definitions(IDHANServer PUBLIC ALLOW_TABLE_DESTRUCTION=1)
endif ()

# Copy page info to the output directory

file(GLOB_RECURSE PAGE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/pages/*")
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

set(CONFIG_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/example.toml")
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