include(proxygen)
include(simdjson)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

AddFGLLibrary(IDHANHttp OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NOT FGL_HAS_REFLECTION)
	message(FATAL_ERROR "IDHANHttp requires C++26 reflection (for GCC, use GCC 16 or newer with -freflection)")
endif ()

target_link_libraries(
		IDHANHttp
		PUBLIC IDHAN IDHANRuntime IDHANProxygen simdjson::simdjson OpenSSL::Crypto folly Folly::folly
		PRIVATE spdlog::spdlog)
