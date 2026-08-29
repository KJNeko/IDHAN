project(IDHANDownloader LANGUAGES C CXX)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

set(QUICKJS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/dependencies/quickjs")
set(ADA_TESTING OFF CACHE BOOL "" FORCE)
set(ADA_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ADA_TOOLS OFF CACHE BOOL "" FORCE)
set(ADA_BUILD_SINGLE_HEADER_LIB OFF CACHE BOOL "" FORCE)
set(ADA_USE_SIMDUTF OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEXBOR_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)

if (NOT EXISTS "${QUICKJS_SOURCE_DIR}/quickjs.c")
	message(FATAL_ERROR "QuickJS is missing; initialize the dependencies/quickjs submodule")
endif ()

add_library(
		IDHANQuickJS
		STATIC
		${QUICKJS_SOURCE_DIR}/quickjs.c
		${QUICKJS_SOURCE_DIR}/dtoa.c
		${QUICKJS_SOURCE_DIR}/libregexp.c
		${QUICKJS_SOURCE_DIR}/libunicode.c
		${QUICKJS_SOURCE_DIR}/cutils.c)

target_include_directories(IDHANQuickJS SYSTEM PUBLIC ${QUICKJS_SOURCE_DIR})
target_compile_definitions(IDHANQuickJS PRIVATE _GNU_SOURCE CONFIG_VERSION="2026-06-04")
target_compile_options(IDHANQuickJS PRIVATE -fwrapv)
target_link_libraries(IDHANQuickJS PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/ada ${CMAKE_CURRENT_BINARY_DIR}/ada EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/lexbor ${CMAKE_CURRENT_BINARY_DIR}/lexbor EXCLUDE_FROM_ALL SYSTEM)

AddFGLLibrary(
		IDHANDownloader
		STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
		IDHANDownloader
		PUBLIC IDHANQuickJS ada::ada Jsoncpp_lib
		PRIVATE IDHAN lexbor_static spdlog::spdlog CURL::libcurl)
target_include_directories(IDHANDownloader SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include)

# quickjs-libc is intentionally omitted. It provides the std/os host APIs,
# including direct filesystem and process access, which parser scripts must not have.

# The premade parsers and the API clients generated from specs/ are staged together, because a
# parser resolves a bare import against the "packages" directory inside its own parser directory.
set(IDHAN_DOWNLOADER_PARSER_DIR ${CMAKE_BINARY_DIR}/bin/downloader CACHE INTERNAL "Staged downloader parsers")
set(IDHAN_DOWNLOADER_PACKAGE_DIR ${IDHAN_DOWNLOADER_PARSER_DIR}/packages)
set(IDHAN_OPENAPI_GENERATOR_VERSION "7.25.0" CACHE STRING "openapi-generator release used for the JS API clients")

find_program(NPM_EXECUTABLE npm)

if (NOT NPM_EXECUTABLE)
	message(FATAL_ERROR "npm is required to bundle the downloader's JavaScript dependencies")
endif ()

add_custom_target(
		IDHANDownloaderPremade
		COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
		${CMAKE_CURRENT_SOURCE_DIR}/premade
		${IDHAN_DOWNLOADER_PARSER_DIR}
		COMMENT "Staging the premade downloader parsers")

file(GLOB SPEC_FILES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/specs/*.yaml
		${CMAKE_CURRENT_SOURCE_DIR}/specs/*.yml
		${CMAKE_CURRENT_SOURCE_DIR}/specs/*.json)

find_program(NPX_EXECUTABLE npx)
find_program(JAVA_EXECUTABLE java)

# Skipping generation leaves a parser that imports a client failing at load time, long after the
# build that caused it, so a missing generator stops the configure rather than warning.
if (NOT BUILD_IDHAN_DOWNLOADER_SPECS)
	set(SPEC_FILES "")
elseif (SPEC_FILES AND (NOT NPX_EXECUTABLE OR NOT JAVA_EXECUTABLE))
	message(FATAL_ERROR "npx and java are required to generate the downloader API clients. "
			"Install node and a JRE, or set -DBUILD_IDHAN_DOWNLOADER_SPECS=OFF to build without "
			"them; parsers importing a generated client will then fail to load.")
endif ()

set(SPEC_STAMPS "")

foreach (SPEC ${SPEC_FILES})
	get_filename_component(SPEC_NAME ${SPEC} NAME_WE)
	set(SPEC_STAMP ${CMAKE_CURRENT_BINARY_DIR}/specs/${SPEC_NAME}.stamp)

	# The spec is third-party, so validation is skipped: a spec that generates a usable client is
	# worth more here than one that satisfies the validator.
	add_custom_command(
			OUTPUT ${SPEC_STAMP}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/specs
			COMMAND ${CMAKE_COMMAND} -E rm -rf ${IDHAN_DOWNLOADER_PACKAGE_DIR}/${SPEC_NAME}
			COMMAND ${CMAKE_COMMAND} -E env OPENAPI_GENERATOR_VERSION=${IDHAN_OPENAPI_GENERATOR_VERSION}
			${NPX_EXECUTABLE} --yes @openapitools/openapi-generator-cli generate
			--skip-validate-spec
			-i ${SPEC}
			-g javascript
			-o ${IDHAN_DOWNLOADER_PACKAGE_DIR}/${SPEC_NAME}
			--additional-properties=projectName=${SPEC_NAME},usePromises=true
			--global-property=apiTests=false,modelTests=false,apiDocs=false,modelDocs=false
			COMMAND ${CMAKE_COMMAND} -E touch ${SPEC_STAMP}
			DEPENDS ${SPEC}
			COMMENT "Generating the ${SPEC_NAME} JavaScript client"
			VERBATIM)

	list(APPEND SPEC_STAMPS ${SPEC_STAMP})
endforeach ()

add_custom_target(IDHANDownloaderSpecs DEPENDS ${SPEC_STAMPS})

set(IDHAN_DOWNLOADER_NPM_DIR ${CMAKE_CURRENT_BINARY_DIR}/npm)
set(IDHAN_DOWNLOADER_ZOD_BUNDLE ${IDHAN_DOWNLOADER_PACKAGE_DIR}/zod.js)

add_custom_command(
		OUTPUT ${IDHAN_DOWNLOADER_ZOD_BUNDLE}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${IDHAN_DOWNLOADER_NPM_DIR}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${IDHAN_DOWNLOADER_PACKAGE_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_CURRENT_SOURCE_DIR}/package.json
		${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json
		${IDHAN_DOWNLOADER_NPM_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		${CMAKE_CURRENT_SOURCE_DIR}/bundles/zod.js
		${IDHAN_DOWNLOADER_NPM_DIR}/zod-entry.js
		COMMAND ${NPM_EXECUTABLE} ci --prefix ${IDHAN_DOWNLOADER_NPM_DIR} --ignore-scripts --no-audit --no-fund
		COMMAND ${NPM_EXECUTABLE} exec --prefix ${IDHAN_DOWNLOADER_NPM_DIR} --offline -- esbuild
		${IDHAN_DOWNLOADER_NPM_DIR}/zod-entry.js
		--bundle
		--format=esm
		--minify
		--platform=browser
		--target=es2023
		--outfile=${IDHAN_DOWNLOADER_ZOD_BUNDLE}
		DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/bundles/zod.js
		${CMAKE_CURRENT_SOURCE_DIR}/package.json
		${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json
		COMMENT "Bundling Zod for downloader scripts"
		VERBATIM)

add_custom_target(IDHANDownloaderZod DEPENDS ${IDHAN_DOWNLOADER_ZOD_BUNDLE})
add_dependencies(IDHANDownloaderZod IDHANDownloaderPremade)
add_custom_target(IDHANDownloaderAssets)
add_dependencies(IDHANDownloaderAssets IDHANDownloaderSpecs IDHANDownloaderZod)

if (BUILD_IDHAN_DOWNLOADER_TESTER)
	find_package(OpenSSL REQUIRED COMPONENTS Crypto)
	AddFGLExecutable(IDHANDownloaderScriptTester ${CMAKE_CURRENT_SOURCE_DIR}/tester)
	target_link_libraries(
			IDHANDownloaderScriptTester
			PRIVATE IDHAN IDHANDownloader Drogon::Drogon OpenSSL::Crypto spdlog::spdlog CURL::libcurl)
	target_include_directories(IDHANDownloaderScriptTester SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include)
	add_dependencies(IDHANDownloaderScriptTester IDHANDownloaderAssets)
endif ()
