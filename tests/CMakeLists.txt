find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)
find_package(GTest REQUIRED)

enable_testing()

AddFGLExecutable(IDHANTests ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(IDHANTests PUBLIC IDHAN GTest::gtest_main IDHANClient libpqxx::pqxx IDHANMigration)

add_dependencies(IDHANTests IDHANServer)

include(CTest)
include(GoogleTest)
gtest_discover_tests(IDHANTests)