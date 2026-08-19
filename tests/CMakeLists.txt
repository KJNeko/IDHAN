find_package(Catch2 3 REQUIRED)
find_package(drogon REQUIRED)

enable_testing()

include(CTest)
include(Catch)

AddFGLExecutable(IDHANDBTests ${CMAKE_CURRENT_SOURCE_DIR}/db)
target_link_libraries(IDHANDBTests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx IDHANMigration)
target_include_directories(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_CURRENT_SOURCE_DIR}/common)
catch_discover_tests(IDHANDBTests)

AddFGLExecutable(IDHANAPITests ${CMAKE_CURRENT_SOURCE_DIR}/api)
target_link_libraries(IDHANAPITests PUBLIC Catch2::Catch2WithMain libpqxx::pqxx drogon IDHAN)
target_include_directories(IDHANAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/api/harness)
add_dependencies(IDHANAPITests IDHANServer)
catch_discover_tests(IDHANAPITests)
