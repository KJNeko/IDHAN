find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(IDHANImportTests importBody.cpp ../src/httpapi/ImportBody.cpp)
target_include_directories(IDHANImportTests PRIVATE ../src)
target_link_libraries(IDHANImportTests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHAN)
catch_discover_tests(IDHANImportTests)
