AddFGLLibrary(IDHAN OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Replacement runtime does not depend on the legacy IDHAN objects.
if (BUILD_IDHAN_SERVER OR BUILD_IDHAN_HTTP OR BUILD_IDHAN_DOWNLOADER)
	add_subdirectory(runtime)
endif ()

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <format>
    int main() {
        std::format(\"Test {}\", 42);
        return 0;
    }
    " HAVE_STD_FORMAT)

if (HAVE_STD_FORMAT)
	target_compile_definitions(IDHAN PUBLIC IDHAN_USE_STD_FORMAT=1)
else ()
	target_compile_definitions(IDHAN PUBLIC IDHAN_USE_STD_FORMAT=0)
endif ()

if (NOT IDHAN_ENABLE_TRACE)
	target_compile_definitions(IDHAN PUBLIC IDHAN_DISABLE_TRACE_LOGGING)
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/HydrusConstants.cmake")

target_link_libraries(IDHAN PUBLIC libFGL Jsoncpp_lib OpenSSL::Crypto)
