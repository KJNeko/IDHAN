set(IDHAN_DOWNLOADER_TEST_PARSER_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures-js")
target_compile_definitions(IDHANDownloaderTests PRIVATE IDHAN_DOWNLOADER_TEST_PARSER_DIR="${IDHAN_DOWNLOADER_TEST_PARSER_DIR}")
file(GLOB_RECURSE PARSER_FIXTURES CONFIGURE_DEPENDS
		"${IDHAN_TEST_SOURCE_DIR}/downloader/fixtures/*.ts"
		"${IDHAN_TEST_SOURCE_DIR}/downloader/contracts/*.ts"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/premade/*.ts"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/premade/packages/*.js"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/premade/packages/*.mjs"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/premade/packages/*.mts"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/premade/*/package.json")
set(PARSER_FIXTURE_INPUT_LIST "${CMAKE_CURRENT_BINARY_DIR}/downloader-fixture-inputs.txt")
file(GENERATE OUTPUT "${PARSER_FIXTURE_INPUT_LIST}" CONTENT "${PARSER_FIXTURES}\n")
set(PARSER_FIXTURE_OUTPUTS
		"${IDHAN_DOWNLOADER_TEST_PARSER_DIR}/.idhan-bundles.json"
		"${IDHAN_DOWNLOADER_TEST_PARSER_DIR}/options.js"
		"${IDHAN_DOWNLOADER_TEST_PARSER_DIR}/packages/contracts.js")
file(GLOB SDK_FIXTURES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/IDHANDownloader/premade/packages/sdk/*.ts")
# Match type-only declarations such as parser.d.ts, which produce no bundle.
list(FILTER SDK_FIXTURES EXCLUDE REGEX "\\.d\\.ts$")
foreach (SDK_SOURCE IN LISTS SDK_FIXTURES)
	get_filename_component(SDK_NAME "${SDK_SOURCE}" NAME_WE)
	list(APPEND PARSER_FIXTURE_OUTPUTS "${IDHAN_DOWNLOADER_TEST_PARSER_DIR}/packages/sdk/${SDK_NAME}.js")
endforeach ()
set(PARSER_FIXTURE_MAPS "")
foreach (FIXTURE_OUTPUT IN LISTS PARSER_FIXTURE_OUTPUTS)
	if (FIXTURE_OUTPUT MATCHES "\\.js$")
		list(APPEND PARSER_FIXTURE_MAPS "${FIXTURE_OUTPUT}.map")
	endif ()
endforeach ()
list(APPEND PARSER_FIXTURE_OUTPUTS ${PARSER_FIXTURE_MAPS})
add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures.stamp" ${PARSER_FIXTURE_OUTPUTS}
		COMMAND ${CMAKE_COMMAND}
		"-DSOURCE=${CMAKE_SOURCE_DIR}"
		"-DWORK=${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures-ts"
		"-DOUTPUT_DIR=${IDHAN_DOWNLOADER_TEST_PARSER_DIR}"
		"-DNPM_DIR=${CMAKE_BINARY_DIR}/IDHANDownloader/npm"
		"-DNODE=${NODE_EXECUTABLE}"
		-P "${IDHAN_TEST_SOURCE_DIR}/downloader/PrepareParserFixtures.cmake"
		COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures.stamp"
		DEPENDS ${PARSER_FIXTURES} "${PARSER_FIXTURE_INPUT_LIST}"
		"${CMAKE_BINARY_DIR}/IDHANDownloader/npm/installed.stamp"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/tsconfig.json"
		"${IDHAN_TEST_SOURCE_DIR}/downloader/PrepareParserFixtures.cmake"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/scripts/build-parsers.mjs"
		VERBATIM)
add_custom_target(IDHANDownloaderTestFixtures DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures.stamp" ${PARSER_FIXTURE_OUTPUTS})
add_dependencies(IDHANDownloaderTestFixtures IDHANDownloaderAssets IDHANDownloaderNpm)
add_dependencies(IDHANDownloaderTests IDHANDownloaderTestFixtures)
