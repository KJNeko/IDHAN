#find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)
find_package(GTest REQUIRED)

enable_testing()

include(CTest)
include(GoogleTest)

AddFGLExecutable(IDHANDBTests ${CMAKE_CURRENT_SOURCE_DIR}/db)
target_link_libraries(IDHANDBTests PUBLIC GTest::gtest_main libpqxx::pqxx IDHANMigration)
target_include_directories(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
gtest_discover_tests(IDHANDBTests)

#AddFGLExecutable(IDHANTests ${CMAKE_CURRENT_SOURCE_DIR}/src)
#target_link_libraries(IDHANTests PUBLIC IDHAN GTest::gtest_main IDHANClient libpqxx::pqxx IDHANMigration)

#target_include_directories(IDHANTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
#target_compile_definitions(IDHANTests PRIVATE
#		IDHAN_WEB_PREFIXES_TS="${CMAKE_SOURCE_DIR}/IDHANWeb/src/api/prefixes.ts")
#add_dependencies(IDHANTests IDHANServer)

#gtest_discover_tests(IDHANTests)

#AddFGLExecutable(IDHANCoreTests ${CMAKE_CURRENT_SOURCE_DIR}/core)
#target_link_libraries(IDHANCoreTests PUBLIC IDHAN GTest::gtest_main PTRCore IDHANModuleIPC)
#gtest_discover_tests(IDHANCoreTests)
