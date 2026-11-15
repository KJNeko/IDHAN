find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(IDHANDownloaderTests direct_image.cpp)
target_link_libraries(IDHANDownloaderTests PRIVATE IDHANDownloader GTest::gtest_main GTest::gmock)
add_test(NAME IDHANDownloader.DirectImage
		COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py
		$<TARGET_FILE:IDHANDownloaderTests>)
set_tests_properties(IDHANDownloader.DirectImage PROPERTIES TIMEOUT 40)