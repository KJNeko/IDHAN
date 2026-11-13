set(IDHAN_DOWNLOADER_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(IDHAN_DOWNLOADER_PARSER_DIR "${CMAKE_BINARY_DIR}/bin/downloaders/parsers" CACHE INTERNAL "Staged downloader parsers")
set(IDHAN_DOWNLOADER_PREBUILT_DIR "" CACHE PATH "Precompiled parser bundles; skips the TypeScript build when provided")
set(IDHAN_DOWNLOADER_NPM_DIR "${CMAKE_CURRENT_BINARY_DIR}/npm")
set(IDHAN_DOWNLOADER_NPM_STAMP "${IDHAN_DOWNLOADER_NPM_DIR}/installed.stamp")

# Runtime-only builds with precompiled assets need no JavaScript build tools.
# Tests still bundle their own TypeScript fixtures before executing C++ tests.
if (NOT IDHAN_DOWNLOADER_PREBUILT_DIR OR BUILD_IDHAN_TESTS)
	find_program(NPM_EXECUTABLE npm REQUIRED)
	find_program(NODE_EXECUTABLE NAMES node nodejs REQUIRED)
	file(GLOB PARSER_PACKAGE_MANIFESTS CONFIGURE_DEPENDS "${IDHAN_DOWNLOADER_SOURCE_DIR}/premade/*/package.json")
	set(PARSER_PACKAGE_COPY_COMMANDS "")
	foreach (PACKAGE_MANIFEST IN LISTS PARSER_PACKAGE_MANIFESTS)
		file(RELATIVE_PATH PACKAGE_PATH "${IDHAN_DOWNLOADER_SOURCE_DIR}" "${PACKAGE_MANIFEST}")
		get_filename_component(PACKAGE_DIRECTORY "${PACKAGE_PATH}" DIRECTORY)
		list(APPEND PARSER_PACKAGE_COPY_COMMANDS
				COMMAND ${CMAKE_COMMAND} -E make_directory "${IDHAN_DOWNLOADER_NPM_DIR}/${PACKAGE_DIRECTORY}"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PACKAGE_MANIFEST}" "${IDHAN_DOWNLOADER_NPM_DIR}/${PACKAGE_PATH}")
	endforeach ()
	add_custom_command(
			OUTPUT "${IDHAN_DOWNLOADER_NPM_STAMP}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${IDHAN_DOWNLOADER_NPM_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${IDHAN_DOWNLOADER_SOURCE_DIR}/package.json"
			"${IDHAN_DOWNLOADER_SOURCE_DIR}/package-lock.json"
			"${IDHAN_DOWNLOADER_NPM_DIR}"
			${PARSER_PACKAGE_COPY_COMMANDS}
			COMMAND ${NPM_EXECUTABLE} ci --prefix "${IDHAN_DOWNLOADER_NPM_DIR}"
			--include=dev --include=optional --ignore-scripts --no-audit --no-fund
			COMMAND ${CMAKE_COMMAND} -E touch "${IDHAN_DOWNLOADER_NPM_STAMP}"
			DEPENDS "${IDHAN_DOWNLOADER_SOURCE_DIR}/package.json"
			"${IDHAN_DOWNLOADER_SOURCE_DIR}/package-lock.json" ${PARSER_PACKAGE_MANIFESTS}
			COMMENT "Installing the parser bundle build tools"
			VERBATIM)
	add_custom_target(IDHANDownloaderNpm DEPENDS "${IDHAN_DOWNLOADER_NPM_STAMP}")
endif ()

if (IDHAN_DOWNLOADER_PREBUILT_DIR)
	get_filename_component(IDHAN_DOWNLOADER_EMITTED_DIR "${IDHAN_DOWNLOADER_PREBUILT_DIR}" ABSOLUTE)
	if (NOT IS_DIRECTORY "${IDHAN_DOWNLOADER_EMITTED_DIR}")
		message(FATAL_ERROR "Precompiled parser bundle directory does not exist: ${IDHAN_DOWNLOADER_EMITTED_DIR}")
	endif ()
	file(GLOB_RECURSE PREBUILT_BUNDLES "${IDHAN_DOWNLOADER_EMITTED_DIR}/*.js" "${IDHAN_DOWNLOADER_EMITTED_DIR}/*.mjs")
	if (NOT PREBUILT_BUNDLES)
		message(FATAL_ERROR "Precompiled parser bundle directory contains no JavaScript: ${IDHAN_DOWNLOADER_EMITTED_DIR}")
	endif ()
else ()
	include("${CMAKE_CURRENT_LIST_DIR}/CompileParsers.cmake")
	set(IDHAN_DOWNLOADER_EMITTED_DIR "${CMAKE_CURRENT_BINARY_DIR}/parser-js")
	idhan_compile_parsers(IDHANDownloaderTypeScript
			"${IDHAN_DOWNLOADER_SOURCE_DIR}/premade"
			"${IDHAN_DOWNLOADER_SOURCE_DIR}/tsconfig.json"
			"${IDHAN_DOWNLOADER_EMITTED_DIR}")
endif ()

add_custom_target(IDHANDownloaderPremade
		COMMAND ${CMAKE_COMMAND}
		"-DSOURCE=${IDHAN_DOWNLOADER_SOURCE_DIR}/premade"
		"-DEMITTED=${IDHAN_DOWNLOADER_EMITTED_DIR}"
		"-DDESTINATION=${IDHAN_DOWNLOADER_PARSER_DIR}"
		"-DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/staged-premade-files.txt"
		-P "${CMAKE_CURRENT_LIST_DIR}/StageParsers.cmake"
		COMMENT "Staging downloader JavaScript and URL classes"
		VERBATIM)
if (NOT IDHAN_DOWNLOADER_PREBUILT_DIR)
	add_dependencies(IDHANDownloaderPremade IDHANDownloaderTypeScript)
endif ()

add_custom_target(IDHANDownloaderAssets)
add_dependencies(IDHANDownloaderAssets IDHANDownloaderPremade)
