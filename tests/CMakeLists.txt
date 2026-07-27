find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)
find_package(GTest REQUIRED)

enable_testing()

AddFGLExecutable(IDHANTests ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(IDHANTests PUBLIC IDHAN GTest::gtest_main IDHANClient libpqxx::pqxx IDHANMigration)

# apiPrefixes.cpp includes the server's header-only prefix list and diffs it against the WebUI's
# copy, so it needs the server's include root and the path to the TypeScript file.
target_include_directories(IDHANTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_compile_definitions(IDHANTests PRIVATE
		IDHAN_WEB_PREFIXES_TS="${CMAKE_SOURCE_DIR}/IDHANWeb/src/api/prefixes.ts")
add_dependencies(IDHANTests IDHANServer)

include(CTest)
include(GoogleTest)
gtest_discover_tests(IDHANTests)