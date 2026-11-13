AddFGLExecutable(IDHANClusterMarkerTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANClusterMarkerTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterMarkerSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/clusters/ClusterAccessSync.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/config/consumeFlag.cpp
		${CMAKE_SOURCE_DIR}/IDHANServer/src/Config.cpp)
target_include_directories(IDHANClusterMarkerTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANClusterMarkerTests PRIVATE Catch2::Catch2WithMain IDHANHttp spdlog::spdlog tomlplusplus::tomlplusplus)
catch_discover_tests(IDHANClusterMarkerTests)
