find_package(Catch2 3 REQUIRED)
find_package(drogon REQUIRED)

enable_testing()

include(CTest)
include(Catch)

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
target_link_libraries(IDHANDBTests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx IDHANMigration IDHAN)
target_include_directories(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/db)
catch_discover_tests(IDHANDBTests)

AddFGLExecutable(IDHANAPITests ${CMAKE_CURRENT_SOURCE_DIR}/api)
target_link_libraries(IDHANAPITests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx drogon IDHAN)
target_include_directories(IDHANAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/api/harness)
target_compile_definitions(IDHANAPITests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# the ugoira scenario refines a mime out of process, so the archive module has to sit beside the server
add_dependencies(IDHANAPITests IDHANServer IDHANModuleRunner IDHANArchive)
catch_discover_tests(IDHANAPITests)
