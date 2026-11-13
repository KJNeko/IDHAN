include_guard(GLOBAL)

set(IDHAN_DEPENDENCIES "${IDHAN_SOURCE_DIR}/dependencies")
list(APPEND CMAKE_MODULE_PATH "${IDHAN_DEPENDENCIES}")
list(APPEND CMAKE_PREFIX_PATH "${IDHAN_DEPENDENCIES}")

add_subdirectory("${IDHAN_DEPENDENCIES}/libFGL" "${IDHAN_BINARY_DIR}/dependencies/libFGL")

find_package(spdlog REQUIRED)
find_package(Jsoncpp REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Create coroutine dependency targets before IDHAN/runtime is configured.
if (BUILD_IDHAN_SERVER OR BUILD_IDHAN_HTTP OR BUILD_IDHAN_DOWNLOADER)
	include(proxygen)
endif ()

if (BUILD_IDHAN_HTTP)
	include(simdjson)
endif ()

if (BUILD_IDHAN_SERVER)
	find_package(PostgreSQL 17 REQUIRED)
	include(postgres)
	include(toml)
endif ()

if (BUILD_IDHAN_TESTS OR BUILD_PTR_CORE_TESTS)
	find_package(Catch2 3 REQUIRED)
	include(Catch)
endif ()
