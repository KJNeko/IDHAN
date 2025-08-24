project(IDHANMigration LANGUAGES C CXX)

set(MIGRATION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/doMigration.cpp")
file(REMOVE ${MIGRATION_SOURCE})

set(MIGRATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE MIGRATION_SQLS CONFIGURE_DEPENDS ${MIGRATION_DIR}/*.sql)

add_custom_command(
		OUTPUT ${MIGRATION_SOURCE}
		DEPENDS ${MIGRATION_SQLS}
		COMMAND ${CMAKE_COMMAND} -DMIGRATION_DIR=${MIGRATION_DIR} -DOUT=${MIGRATION_SOURCE} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateMigrations.cmake"
		COMMENT "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateMigrations.cmake: Generating doMigration.cpp")

file(GLOB_RECURSE CPP_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/src/**.cpp
)

add_library(IDHANMigration STATIC ${MIGRATION_SOURCE} ${CPP_SOURCES})
target_include_directories(IDHANMigration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(IDHANMigration PRIVATE pqxx spdlog::spdlog)
