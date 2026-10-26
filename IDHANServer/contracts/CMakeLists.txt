# These interfaces intentionally have no implementation or link test yet.
find_package(PostgreSQL REQUIRED)
include(simdjson)

add_library(IDHANReplacementInterfaces INTERFACE)
target_include_directories(IDHANReplacementInterfaces INTERFACE
		${PROJECT_SOURCE_DIR}/include
		${CMAKE_SOURCE_DIR}/IDHANDb/include
		${CMAKE_SOURCE_DIR}/IDHANHttp/include
		${CMAKE_SOURCE_DIR}/IDHANModules/ipc/include
		${CMAKE_SOURCE_DIR}/IDHANDownloader/include)
target_link_libraries(IDHANReplacementInterfaces INTERFACE
		IDHANRuntime IDHANProxygen PostgreSQL::PostgreSQL simdjson::simdjson)

add_library(IDHANReplacementContracts OBJECT EXCLUDE_FROM_ALL
		Database.cpp HTTP.cpp ModuleIPC.cpp Server.cpp)
target_link_libraries(IDHANReplacementContracts PRIVATE IDHANReplacementInterfaces)
