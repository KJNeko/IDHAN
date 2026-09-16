AddFGLLibrary(
		IDHANDb
		STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(PostgreSQL 17 REQUIRED)
include(proxygen)

target_link_libraries(IDHANDb
		PUBLIC IDHANRuntime PostgreSQL::PostgreSQL Folly::folly_coro_task Folly::folly_io_async_async_base
		PRIVATE Folly::folly_coro_bounded_queue Folly::folly_coro_baton
		Folly::folly_executors_cpu_thread_pool_executor Folly::folly_executors_thread_factory_named_thread_factory)

if (BUILD_IDHAN_TESTS)
	add_subdirectory(test)
endif ()
