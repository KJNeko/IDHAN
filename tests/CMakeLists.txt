

enable_testing()

file(GLOB_RECURSE FGL_TEST_SOURCES "*.cpp")

add_executable(IDHANTests ${FGL_TEST_SOURCES})
target_link_libraries(IDHANTests PUBLIC IDHAN Catch2::Catch2WithMain IDHANServer IDHANClient)

include(CTest)
include(Catch)
catch_discover_tests(IDHANTests)