find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB_RECURSE IDHAN_DOWNLOADER_TEST_SOURCES CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(IDHANDownloaderTests ${IDHAN_DOWNLOADER_TEST_SOURCES})
target_link_libraries(IDHANDownloaderTests PRIVATE IDHANDownloader GTest::gtest_main GTest::gmock)
target_compile_definitions(IDHANDownloaderTests PRIVATE IDHAN_TEST_PARSER_DIR="${IDHAN_DOWNLOADER_PARSER_DIR}")
add_dependencies(IDHANDownloaderTests IDHANDownloaderAssets)

add_test(NAME IDHANDownloader.Tests
		COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py
		$<TARGET_FILE:IDHANDownloaderTests>)

set_tests_properties(IDHANDownloader.Tests PROPERTIES TIMEOUT 40)
