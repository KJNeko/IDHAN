AddFGLExecutable(IDHANDBTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/db/tagCounts.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/bandwidth.cpp)
target_compile_definitions(IDHANDBTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(IDHANDBTests PRIVATE Catch2::Catch2WithMain libpqxx::pqxx IDHANMigration IDHAN IDHANDb IDHANDownloader
		Folly::folly_executors_cpu_thread_pool_executor)
target_include_directories(IDHANDBTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANDb/include ${IDHAN_TEST_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(IDHANDBTests)
