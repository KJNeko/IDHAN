
set(CMAKE_AUTOMOC ON)

#if (BUILD_HYDRUS_IMPORTER ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)
#else ()
#	find_package(Qt6 REQUIRED COMPONENTS Core)
#endif ()
