include(proxygen)
include(simdjson)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

AddFGLLibrary(IDHANHttp OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
		IDHANHttp
		PUBLIC IDHAN IDHANRuntime IDHANProxygen simdjson::simdjson OpenSSL::Crypto folly Folly::folly
		PRIVATE spdlog::spdlog)

if (BUILD_IDHAN_TESTS)
	add_subdirectory(test)
endif ()
