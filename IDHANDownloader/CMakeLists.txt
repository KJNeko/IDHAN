project(IDHANDownloader LANGUAGES C CXX)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(QUICKJS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/dependencies/quickjs")
set(ADA_TESTING OFF CACHE BOOL "" FORCE)
set(ADA_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(ADA_TOOLS OFF CACHE BOOL "" FORCE)
set(ADA_BUILD_SINGLE_HEADER_LIB OFF CACHE BOOL "" FORCE)
set(ADA_USE_SIMDUTF OFF CACHE BOOL "" FORCE)
set(ADA_INCLUDE_URL_PATTERN ON CACHE BOOL "" FORCE)
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
		PUBLIC IDHANQuickJS ada::ada Jsoncpp_lib Folly::folly_executors_cpu_thread_pool_executor
		Folly::folly_io_async_async_base Folly::folly_coro_task Folly::folly_coro_baton Folly::folly_coro_async_scope Folly::folly_coro_mutex
		PRIVATE IDHAN lexbor_static spdlog::spdlog CURL::libcurl OpenSSL::Crypto)
target_include_directories(IDHANDownloader SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/dependencies/ada/include)

# quickjs-libc is intentionally omitted. It provides the std/os host APIs,
# including direct filesystem and process access, which parser scripts must not have.

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ParserAssets.cmake)

if (BUILD_IDHAN_DOWNLOADER_TESTER)
	find_package(OpenSSL REQUIRED COMPONENTS Crypto)
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
