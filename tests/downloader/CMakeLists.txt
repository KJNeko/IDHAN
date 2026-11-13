AddFGLExecutable(IDHANDownloaderTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANDownloaderTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/downloader/DownloadBuffer.cpp)
target_include_directories(IDHANDownloaderTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANDownloaderTests PRIVATE Catch2::Catch2WithMain IDHANDownloader IDHAN IDHANRuntime Folly::folly_file Folly::folly_coro_blocking_wait)
target_include_directories(
		IDHANDownloaderTests
		PRIVATE ${CMAKE_SOURCE_DIR}/IDHANDownloader/src ${CMAKE_CURRENT_SOURCE_DIR}/harness)
target_include_directories(
		IDHANDownloaderTests
		SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include ${CMAKE_SOURCE_DIR}/dependencies/quickjs)
target_compile_definitions(IDHANDownloaderTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(
		IDHANDownloaderTests PRIVATE IDHAN_DOWNLOADER_PARSER_DIR="${IDHAN_DOWNLOADER_PARSER_DIR}")
include("${CMAKE_CURRENT_SOURCE_DIR}/ParserFixtures.cmake")
catch_discover_tests(IDHANDownloaderTests)
