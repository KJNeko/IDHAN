project(IDHANServer LANGUAGES CXX C)

AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(IDHANServer PRIVATE ${MIGRATION_SOURCE})

find_package(spdlog REQUIRED)
find_package(drogon REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

target_link_libraries(IDHANServer PUBLIC spdlog::spdlog drogon)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_link_libraries(IDHANServer PRIVATE uring)
endif ()
target_link_libraries(IDHANServer PRIVATE libpqxx::pqxx tomlplusplus::tomlplusplus)
target_link_libraries(IDHANServer PRIVATE IDHAN IDHANDownloader)
target_link_libraries(IDHANServer PUBLIC IDHANModules IDHANModuleIPC IDHANMigration)
target_link_libraries(IDHANServer PUBLIC OpenSSL::Crypto)
target_compile_definitions(IDHANServer PUBLIC IDHAN_USE_STD_FORMAT)
target_compile_definitions(
		IDHANServer
		PRIVATE IDHAN_DOWNLOADER_DEFAULT_PARSER_DIRECTORY="${IDHAN_DOWNLOADER_PARSER_DIR}")

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
add_dependencies(IDHANServer IDHANModuleRunner)
