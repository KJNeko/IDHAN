find_package(Catch2 3 REQUIRED)
find_package(CURL REQUIRED)

enable_testing()

include(CTest)
include(Catch)

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
target_link_libraries(IDHANDownloaderTests PUBLIC Catch2::Catch2WithMain IDHANDownloader)
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
add_dependencies(IDHANDownloaderTests IDHANDownloaderAssets)
catch_discover_tests(IDHANDownloaderTests)

AddFGLExecutable(IDHANDBTests ${CMAKE_CURRENT_SOURCE_DIR}/db)
target_sources(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/db/tagCounts.cpp)
target_compile_definitions(IDHANDBTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(IDHANDBTests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx IDHANMigration IDHAN IDHANDb
		Folly::folly_executors_cpu_thread_pool_executor)
target_include_directories(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANDb/include ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/db)
catch_discover_tests(IDHANDBTests)

AddFGLExecutable(IDHANHashMaintenanceTests ${CMAKE_CURRENT_SOURCE_DIR}/hashMaintenance)
target_sources(IDHANHashMaintenanceTests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/db/CurrentTestName.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/hashMaintenance.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/hamming/hammingQueue.cpp)
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
		${CMAKE_SOURCE_DIR}/IDHANServer/src/httpapi/jsonBody.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/httpapi/parameterDeclarations.cpp)
target_include_directories(IDHANParameterTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANParameterTests PRIVATE Catch2::Catch2WithMain IDHANHttp Folly::folly spdlog::spdlog)
catch_discover_tests(IDHANParameterTests)

AddFGLExecutable(IDHANClusterMarkerTests ${CMAKE_CURRENT_SOURCE_DIR}/clusterMarkers)
target_sources(IDHANClusterMarkerTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterMarker.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterAccess.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/config/consumeFlag.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/Config.cpp)
target_include_directories(IDHANClusterMarkerTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANClusterMarkerTests PRIVATE Catch2::Catch2WithMain IDHANHttp spdlog::spdlog tomlplusplus::tomlplusplus)
catch_discover_tests(IDHANClusterMarkerTests)
