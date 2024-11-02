

enable_testing()


AddFGLExecutable(IDHANTests ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(IDHANTests PUBLIC IDHAN Catch2::Catch2WithMain LibIDHANServer LibIDHANClient)

#catch_discover_tests(IDHANTests)