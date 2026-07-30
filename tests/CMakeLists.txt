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

# Plan-1 coro/io suites. Deliberately separate from IDHANTests: that target globs sources which call
# into IDHANServer executable code (SearchBuilder, AuthEndpoint) and has never linked. This one
# depends on IDHAN and GoogleTest only, so it needs neither drogon nor a live PostgreSQL.
AddFGLExecutable(IDHANCoreTests ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(IDHANCoreTests PUBLIC IDHAN GTest::gtest_main PTRCore)
gtest_discover_tests(IDHANCoreTests)