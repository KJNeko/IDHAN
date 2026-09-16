find_package(Catch2 3 REQUIRED)
find_package(CURL REQUIRED)

include(Catch)

AddFGLExecutable(IDHANHttpTests ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(IDHANHttpTests PRIVATE Catch2::Catch2WithMain CURL::libcurl IDHANHttp)
target_include_directories(IDHANHttpTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
catch_discover_tests(IDHANHttpTests)
