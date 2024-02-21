

enable_testing()

file(GLOB_RECURSE FGL_TEST_SOURCES "*.cpp")

add_executable(IDHANTests ${FGL_TEST_SOURCES})
target_link_libraries(IDHANTests IDHAN Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(IDHANTests)