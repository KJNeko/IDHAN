find_package(Catch2 3 REQUIRED)
find_package(CURL REQUIRED)

enable_testing()

include(CTest)
include(Catch)

AddFGLExecutable(IDHANHttpTests ${CMAKE_CURRENT_SOURCE_DIR}/http)
target_link_libraries(IDHANHttpTests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHAN CURL::libcurl)
target_include_directories(IDHANHttpTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
catch_discover_tests(IDHANHttpTests)

AddFGLExecutable(IDHANSharedTests ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(IDHANSharedTests PRIVATE Catch2::Catch2WithMain IDHAN)
catch_discover_tests(IDHANSharedTests)

AddFGLExecutable(IDHANImporterProgressTests ${CMAKE_CURRENT_SOURCE_DIR}/hydrusImporter)
target_sources(IDHANImporterProgressTests PRIVATE
		${CMAKE_SOURCE_DIR}/tools/HydrusImporter/src/gui/hydrus/ImportProgress.cpp)
target_include_directories(IDHANImporterProgressTests PRIVATE ${CMAKE_SOURCE_DIR}/tools/HydrusImporter/src)
target_link_libraries(IDHANImporterProgressTests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(IDHANImporterProgressTests)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	AddFGLExecutable(IDHANFilesystemTests ${CMAKE_CURRENT_SOURCE_DIR}/filesystem)
	target_sources(IDHANFilesystemTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/renameNoReplace.cpp)
	target_sources(IDHANFilesystemTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clearThumbnailCache.cpp)
	target_include_directories(IDHANFilesystemTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
	target_link_libraries(IDHANFilesystemTests PRIVATE Catch2::Catch2WithMain)
	catch_discover_tests(IDHANFilesystemTests)
endif ()

AddFGLExecutable(IDHANModuleTests ${CMAKE_CURRENT_SOURCE_DIR}/modules)
target_link_libraries(IDHANModuleTests PUBLIC Catch2::Catch2WithMain IDHANModuleIPC IDHANModules IDHAN)
target_compile_definitions(IDHANModuleTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# psd.cpp is compiled in directly: openWholeFile lives in the IDHANPsd module, which is a loadable
# module rather than a library anything can link.
target_sources(IDHANModuleTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANModules/premade/psd/psd.cpp)
target_include_directories(IDHANModuleTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANModules/premade/psd)
add_dependencies(IDHANModuleTests IDHANVips)
add_test(NAME IDHANModuleTests COMMAND IDHANModuleTests)

AddFGLExecutable(IDHANDownloaderTests ${CMAKE_CURRENT_SOURCE_DIR}/downloader)
target_sources(IDHANDownloaderTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/downloader/DownloadBuffer.cpp)
target_include_directories(IDHANDownloaderTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANDownloaderTests PUBLIC Catch2::Catch2WithMain IDHANDownloader
		PRIVATE IDHAN IDHANRuntime Folly::folly_file Folly::folly_coro_blocking_wait)
# The rewrite keeps everything but the host seams inside src/, so the tests reach the lane, cookie
# and registry internals the same way IDHANDBTests reaches into IDHANServer/src.
target_include_directories(
		IDHANDownloaderTests
		PRIVATE ${CMAKE_SOURCE_DIR}/IDHANDownloader/src ${CMAKE_CURRENT_SOURCE_DIR}/downloader/harness)
target_include_directories(
		IDHANDownloaderTests
		SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include ${CMAKE_SOURCE_DIR}/dependencies/quickjs)
target_compile_definitions(IDHANDownloaderTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(
		IDHANDownloaderTests PRIVATE IDHAN_DOWNLOADER_PARSER_DIR="${IDHAN_DOWNLOADER_PARSER_DIR}")
set(IDHAN_DOWNLOADER_TEST_PARSER_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures-js")
target_compile_definitions(IDHANDownloaderTests PRIVATE IDHAN_DOWNLOADER_TEST_PARSER_DIR="${IDHAN_DOWNLOADER_TEST_PARSER_DIR}")
file(GLOB_RECURSE PARSER_FIXTURES CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/downloader/fixtures/*.ts"
		"${CMAKE_CURRENT_SOURCE_DIR}/downloader/contracts/*.ts"
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
		-P "${CMAKE_CURRENT_SOURCE_DIR}/downloader/PrepareParserFixtures.cmake"
		COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures.stamp"
		DEPENDS ${PARSER_FIXTURES} "${PARSER_FIXTURE_INPUT_LIST}"
		"${CMAKE_BINARY_DIR}/IDHANDownloader/npm/installed.stamp"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/tsconfig.json"
		"${CMAKE_CURRENT_SOURCE_DIR}/downloader/PrepareParserFixtures.cmake"
		"${CMAKE_SOURCE_DIR}/IDHANDownloader/scripts/build-parsers.mjs"
		VERBATIM)
add_custom_target(IDHANDownloaderTestFixtures DEPENDS
		"${CMAKE_CURRENT_BINARY_DIR}/downloader-fixtures.stamp" ${PARSER_FIXTURE_OUTPUTS})
add_dependencies(IDHANDownloaderTestFixtures IDHANDownloaderAssets IDHANDownloaderNpm)
add_dependencies(IDHANDownloaderTests IDHANDownloaderTestFixtures)
catch_discover_tests(IDHANDownloaderTests)

AddFGLExecutable(IDHANFileRecoveryTests ${CMAKE_CURRENT_SOURCE_DIR}/fileRecovery)
target_sources(IDHANFileRecoveryTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/urls/AssociateUrls.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/fileInfo.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/PathHelpers.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/getFileTimes.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/recordFilePath.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterAccessSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterManager.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterMarkerSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/clusterIdentity.cpp)
target_include_directories(IDHANFileRecoveryTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(IDHANFileRecoveryTests PRIVATE Catch2::Catch2WithMain libpqxx::pqxx
		IDHANDb IDHAN IDHANDownloader spdlog::spdlog Jsoncpp_lib Folly::folly_file Folly::folly_coro_mutex
		Folly::folly_executors_cpu_thread_pool_executor)
catch_discover_tests(IDHANFileRecoveryTests)

AddFGLExecutable(IDHANDBTests ${CMAKE_CURRENT_SOURCE_DIR}/db)
target_sources(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/db/tagCounts.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/bandwidth.cpp)
target_compile_definitions(IDHANDBTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(IDHANDBTests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx IDHANMigration IDHAN IDHANDb IDHANDownloader
		Folly::folly_executors_cpu_thread_pool_executor)
target_include_directories(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANDb/include ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/db)
catch_discover_tests(IDHANDBTests)

AddFGLExecutable(IDHANHashMaintenanceTests ${CMAKE_CURRENT_SOURCE_DIR}/hashMaintenance)
target_sources(IDHANHashMaintenanceTests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/db/CurrentTestName.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/hashMaintenance.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/hamming/hammingQueue.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/hamming/PhashIndex.cpp)
target_include_directories(IDHANHashMaintenanceTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANModules/include
		${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/db)
target_link_libraries(IDHANHashMaintenanceTests PRIVATE Catch2::Catch2WithMain libpqxx::pqxx
		IDHANMigration IDHANDb IDHAN spdlog::spdlog
		Folly::folly_executors_cpu_thread_pool_executor)
catch_discover_tests(IDHANHashMaintenanceTests)

AddFGLExecutable(IDHANAPITests ${CMAKE_CURRENT_SOURCE_DIR}/api)
target_sources(IDHANAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/PathHelpers.cpp)
target_link_libraries(IDHANAPITests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx CURL::libcurl IDHAN)
target_include_directories(IDHANAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANDb/include ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/api/harness)
target_compile_definitions(IDHANAPITests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# the ugoira scenario refines a mime out of process, so the archive module has to sit beside the server
add_dependencies(IDHANAPITests IDHANServer IDHANModuleRunner IDHANArchive)
catch_discover_tests(IDHANAPITests)

AddFGLExecutable(IDHANParameterTests ${CMAKE_CURRENT_SOURCE_DIR}/parameters)
target_sources(IDHANParameterTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/httpapi/jsonBody.cpp)
target_include_directories(IDHANParameterTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANParameterTests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHANDb Folly::folly spdlog::spdlog)
catch_discover_tests(IDHANParameterTests)

AddFGLExecutable(IDHANOpenAPITests ${CMAKE_CURRENT_SOURCE_DIR}/openapi)
target_include_directories(IDHANOpenAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANOpenAPITests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHANOpenAPIDocument)
target_compile_options(IDHANOpenAPITests PRIVATE
		$<$<CXX_COMPILER_ID:Clang>:-fconstexpr-steps=10000000>)
add_dependencies(IDHANOpenAPITests IDHANOpenAPI)
catch_discover_tests(IDHANOpenAPITests)

set(IDHAN_ROUTE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/routes/fixtures")
set(IDHAN_ROUTE_TEST_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/route-discovery")
file(GLOB_RECURSE IDHAN_ROUTE_FIXTURE_INPUTS CONFIGURE_DEPENDS "${IDHAN_ROUTE_FIXTURES}/*.hpp")
add_custom_command(OUTPUT "${IDHAN_ROUTE_TEST_GENERATED}/gen_routes.hpp"
		COMMAND "${CMAKE_COMMAND}" "-DIDHAN_ROUTE_ROOT=${IDHAN_ROUTE_FIXTURES}"
		"-DIDHAN_ROUTE_OUTPUT=${IDHAN_ROUTE_TEST_GENERATED}/gen_routes.hpp"
		-P "${CMAKE_SOURCE_DIR}/IDHANServer/cmake/GenerateRoutes.cmake"
		DEPENDS ${IDHAN_ROUTE_FIXTURE_INPUTS} "${CMAKE_SOURCE_DIR}/IDHANServer/cmake/GenerateRoutes.cmake"
		VERBATIM)
add_custom_target(IDHANTestGeneratedRoutes DEPENDS "${IDHAN_ROUTE_TEST_GENERATED}/gen_routes.hpp")
AddFGLExecutable(IDHANRouteDiscoveryTests ${CMAKE_CURRENT_SOURCE_DIR}/routes/runtime)
target_include_directories(IDHANRouteDiscoveryTests PRIVATE "${IDHAN_ROUTE_TEST_GENERATED}" "${IDHAN_ROUTE_FIXTURES}")
target_link_libraries(IDHANRouteDiscoveryTests PRIVATE Catch2::Catch2WithMain IDHANHttp)
add_dependencies(IDHANRouteDiscoveryTests IDHANTestGeneratedRoutes)
catch_discover_tests(IDHANRouteDiscoveryTests)
add_test(NAME IDHANRouteHeaderGenerator
		COMMAND "${CMAKE_COMMAND}" "-DIDHAN_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/route-generator-tests"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/routes/test_generator.cmake")

AddFGLExecutable(IDHANClusterMarkerTests ${CMAKE_CURRENT_SOURCE_DIR}/clusterMarkers)
target_sources(IDHANClusterMarkerTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterMarkerSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterAccessSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/config/consumeFlag.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/Config.cpp)
target_include_directories(IDHANClusterMarkerTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANClusterMarkerTests PRIVATE Catch2::Catch2WithMain IDHANHttp spdlog::spdlog tomlplusplus::tomlplusplus)
catch_discover_tests(IDHANClusterMarkerTests)
