
find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)
find_package(Catch2 REQUIRED)

enable_testing()

AddFGLExecutable(IDHANTests ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(IDHANTests PUBLIC IDHAN Catch2::Catch2WithMain IDHANClient libpqxx::pqxx)

add_dependencies(IDHANTests IDHANServer)

#catch_discover_tests(IDHANTests)