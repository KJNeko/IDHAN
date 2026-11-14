AddFGLExecutable(IDHANSharedTests ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(IDHANSharedTests PRIVATE GTest::gtest_main IDHAN)
gtest_discover_tests(IDHANSharedTests)
