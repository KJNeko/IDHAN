AddFGLExecutable(IDHANHttpTests ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(IDHANHttpTests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHAN CURL::libcurl)
target_include_directories(IDHANHttpTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
catch_discover_tests(IDHANHttpTests)
