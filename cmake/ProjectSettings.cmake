include_guard(GLOBAL)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# No targets use named C++ modules. Scanning prevents ccache hits with GCC.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "Scan C++ sources for module dependencies")

# Dependencies default to static libraries; IDHANClient explicitly remains shared.
set(BUILD_STATIC_LIBS_ONLY ON)
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)
