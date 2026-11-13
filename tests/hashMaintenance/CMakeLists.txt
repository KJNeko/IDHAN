AddFGLExecutable(IDHANHashMaintenanceTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANHashMaintenanceTests PRIVATE
		${IDHAN_TEST_SOURCE_DIR}/db/CurrentTestName.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/hashMaintenance.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/hamming/hammingQueue.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/hamming/PhashIndex.cpp)
target_include_directories(IDHANHashMaintenanceTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANModules/include
		${IDHAN_TEST_SOURCE_DIR}/common ${IDHAN_TEST_SOURCE_DIR}/db)
target_link_libraries(IDHANHashMaintenanceTests PRIVATE Catch2::Catch2WithMain libpqxx::pqxx
		IDHANMigration IDHANDb IDHAN spdlog::spdlog
		Folly::folly_executors_cpu_thread_pool_executor)
catch_discover_tests(IDHANHashMaintenanceTests)
