AddFGLExecutable(IDHANFileRecoveryTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANFileRecoveryTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/urls/AssociateUrls.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/db/fileInfo.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/PathHelpers.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/getFileTimes.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/recordFilePath.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterAccessSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterManager.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterMarkerSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/clusterIdentity.cpp)
target_include_directories(IDHANFileRecoveryTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src ${IDHAN_TEST_SOURCE_DIR}/common)
target_link_libraries(IDHANFileRecoveryTests PRIVATE Catch2::Catch2WithMain libpqxx::pqxx
		IDHANDb IDHAN IDHANDownloader spdlog::spdlog Jsoncpp_lib Folly::folly_file Folly::folly_coro_mutex
		Folly::folly_executors_cpu_thread_pool_executor)
catch_discover_tests(IDHANFileRecoveryTests)
