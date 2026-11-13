include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

AddFGLLibrary(
		IDHANDownloader
		STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
		IDHANDownloader
		PUBLIC IDHANQuickJS ada::ada Jsoncpp_lib Folly::folly_executors_cpu_thread_pool_executor
		Folly::folly_io_async_async_base Folly::folly_coro_task Folly::folly_coro_baton Folly::folly_coro_async_scope Folly::folly_coro_mutex
		PRIVATE IDHAN lexbor_static spdlog::spdlog CURL::libcurl OpenSSL::Crypto)
target_include_directories(IDHANDownloader SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include)

# quickjs-libc is intentionally omitted. It provides the std/os host APIs,
# including direct filesystem and process access, which parser scripts must not have.

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ParserAssets.cmake)

if (BUILD_IDHAN_DOWNLOADER_TESTER)
	AddFGLExecutable(IDHANDownloaderScriptTester ${CMAKE_CURRENT_SOURCE_DIR}/tester)
	file(GLOB_RECURSE IDHAN_TESTER_FILE_IO_SOURCES CONFIGURE_DEPENDS
			"${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/io/*.cpp")
	target_sources(IDHANDownloaderScriptTester PRIVATE ${IDHAN_TESTER_FILE_IO_SOURCES})
	target_include_directories(IDHANDownloaderScriptTester PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
	if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
		target_link_libraries(IDHANDownloaderScriptTester PRIVATE uring)
	endif ()
	target_link_libraries(
			IDHANDownloaderScriptTester
			PRIVATE IDHAN IDHANRuntime IDHANDownloader OpenSSL::Crypto spdlog::spdlog CURL::libcurl Folly::folly_file)
	target_include_directories(IDHANDownloaderScriptTester SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include)
	add_dependencies(IDHANDownloaderScriptTester IDHANDownloaderAssets)
endif ()
