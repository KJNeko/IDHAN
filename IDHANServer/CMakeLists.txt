AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SERVER_SQL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.sql")
set_source_files_properties(${SERVER_SQL_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(IDHANServer PRIVATE ${SERVER_SQL_FILES})

target_link_libraries(IDHANServer PRIVATE spdlog::spdlog)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(IDHANServer PRIVATE uring)
endif ()
target_link_libraries(IDHANServer PRIVATE libpqxx::pqxx tomlplusplus::tomlplusplus)
target_link_libraries(IDHANServer PRIVATE IDHAN IDHANDownloader)
target_link_libraries(IDHANServer PRIVATE IDHANHttp IDHANDb)
target_link_libraries(IDHANServer PRIVATE IDHANModules IDHANModuleIPC IDHANMigration)
add_dependencies(IDHANServer IDHANDownloaderAssets)

if (IDHAN_HARDEN)
	target_compile_definitions(IDHANServer PRIVATE IDHAN_HARDEN)
endif ()

if (IDHAN_DISABLE_API_AUTH)
	target_compile_definitions(IDHANServer PRIVATE IDHAN_DISABLE_API_AUTH)
endif ()

if (IDHAN_USE_JEMALLOC)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(JEMALLOC REQUIRED IMPORTED_TARGET jemalloc)
	target_link_libraries(IDHANServer PRIVATE PkgConfig::JEMALLOC)
	message(STATUS "IDHANServer: using jemalloc ${JEMALLOC_VERSION}")
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Assets.cmake")

add_dependencies(IDHANServer IDHANPremadeModules)
add_dependencies(IDHANServer IDHANModuleRunner)

add_subdirectory(runtime)
add_subdirectory(contracts)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileOpenAPI.cmake")
