set(MIGRATION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/doMigration.cpp")

set(MIGRATION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE MIGRATION_SQLS CONFIGURE_DEPENDS "${MIGRATION_DIR}/*.sql")
# A removed SQL file must invalidate the generated source as well as an edited one.
set(MIGRATION_INPUT_LIST "${CMAKE_CURRENT_BINARY_DIR}/migration-inputs.txt")
file(GENERATE OUTPUT "${MIGRATION_INPUT_LIST}" CONTENT "${MIGRATION_SQLS}\n")

add_custom_command(
		OUTPUT "${MIGRATION_SOURCE}"
		DEPENDS ${MIGRATION_SQLS} "${MIGRATION_INPUT_LIST}"
		${CMAKE_CURRENT_SOURCE_DIR}/src/migration-template.cpp.unused
		${CMAKE_CURRENT_SOURCE_DIR}/src/check-template.cpp.unused
		${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateMigrations.cmake
		COMMAND "${CMAKE_COMMAND}" "-DMIGRATION_DIR=${MIGRATION_DIR}" "-DOUT=${MIGRATION_SOURCE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateMigrations.cmake"
		COMMENT "Generating database migrations"
		VERBATIM)

file(GLOB_RECURSE CPP_SOURCES CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(IDHANMigration STATIC ${MIGRATION_SOURCE} ${CPP_SOURCES})
target_include_directories(IDHANMigration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(IDHANMigration PUBLIC IDHANDb libpqxx::pqxx PRIVATE spdlog::spdlog)
