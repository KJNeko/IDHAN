
AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(MIGRATION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/doMigration.cpp")
file(REMOVE ${MIGRATION_SOURCE})

target_sources(IDHANServer PRIVATE ${MIGRATION_SOURCE})

set(MIGRATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/db/setup/migration)

file(GLOB_RECURSE MIGRATION_SQLS ${MIGRATION_DIR}/*.sql)

add_custom_command(
		OUTPUT ${MIGRATION_SOURCE}
		DEPENDS ${MIGRATION_SQLS}
		COMMAND ${CMAKE_COMMAND} -DMIGRATION_DIR=${MIGRATION_DIR} -DOUT=${MIGRATION_SOURCE} -P "${CMAKE_CURRENT_SOURCE_DIR}/modules/GenerateMigrations.cmake"
		COMMENT "${CMAKE_CURRENT_SOURCE_DIR}/modules/GenerateMigrations.cmake: Generating doMigration.cpp")

target_link_libraries(IDHANServer PUBLIC spdlog fmt drogon)
target_link_libraries(IDHANServer PRIVATE pqxx sqlite3 Qt6::Core)
target_link_libraries(IDHANServer PRIVATE IDHAN)
target_include_directories(IDHANServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (DEFINED ALLOW_TABLE_DESTRUCTION AND ALLOW_TABLE_DESTRUCTION)
	target_compile_definitions(IDHANServer PUBLIC ALLOW_TABLE_DESTRUCTION=1)
endif ()

# Copy page info to the output directory

file(GLOB_RECURSE PAGE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/pages/*")

foreach (PAGE ${PAGE_FILES})

	file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src ${PAGE})

	set(OUT_PATH ${CMAKE_BINARY_DIR}/bin/${REL_PATH})

	add_custom_command(
			TARGET IDHANServer POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy ${PAGE} ${OUT_PATH}
			DEPENDS ${PAGE}
			COMMENT "Copying ${PAGE} to ${OUT_PATH}"
	)
endforeach ()

