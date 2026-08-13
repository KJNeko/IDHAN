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
target_link_libraries(IDHANServer PRIVATE IDHAN)
target_link_libraries(IDHANServer PUBLIC IDHANModules IDHANModuleIPC IDHANMigration)
target_link_libraries(IDHANServer PUBLIC OpenSSL::Crypto)
target_compile_definitions(IDHANServer PUBLIC IDHAN_USE_STD_FORMAT)

# TEMPORARY (coroutine frame leak hunt): live coroutine frame accounting, exposed via GET /coro/frames.
# Always on in Debug builds and absent everywhere else -- no feature flag to remember to set; the root
# CMakeLists.txt sets IDHAN_TRACK_CORO_FRAMES from CMAKE_BUILD_TYPE. A frame that is never destroyed
# keeps every local held across a co_await alive with it, so a live count that climbs and never settles
# names the leaking coroutine type directly. Remove dependencies/coroprobe/,
# dependencies/patches/drogon-coro-frame-probe.patch, src/api/maintenance/coroFrames.cpp, the option,
# and the block in dependencies/Finddrogon.cmake together once the leak is found.
#
# IDHAN_TRACK_CORO_FRAMES itself is deliberately NOT defined here: it also changes drogon::Task's
# promise_type via the coroutine.h shim, so it has to be uniform across drogon and every consumer or it
# is an ODR violation. It is set PUBLIC on the drogon target in dependencies/Finddrogon.cmake and
# reaches this target through that link, which is the only place it may be set.
#
# The include dir is added unconditionally, though: coroFrames.cpp includes the probe header whether or
# not tracking is on (the header degrades to an empty struct when the define is absent), and drogon only
# exports this dir in the ON case.
target_include_directories(IDHANServer PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/coroprobe/include)

if (IDHAN_HARDEN)
	target_compile_definitions(IDHANServer PRIVATE IDHAN_HARDEN)
endif ()

if (IDHAN_DISABLE_API_AUTH)
	target_compile_definitions(IDHANServer PRIVATE IDHAN_DISABLE_API_AUTH)
endif ()

# jemalloc interposes malloc/free process-wide, so linking it here also covers libvips and everything
# else the dlopen'd modules allocate through -- there is nothing to link into the modules themselves.
# Requested explicitly, so a missing jemalloc is an error rather than a silent fall back to glibc.
if (IDHAN_USE_JEMALLOC)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(JEMALLOC REQUIRED IMPORTED_TARGET jemalloc)
	target_link_libraries(IDHANServer PRIVATE PkgConfig::JEMALLOC)
	message(STATUS "IDHANServer: using jemalloc ${JEMALLOC_VERSION}")
endif ()

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
# The server spawns this per module library; it must exist before the server is useful.
add_dependencies(IDHANServer IDHANModuleRunner)

