add_library(IDHANServerRuntime STATIC Executors.cpp)
target_include_directories(IDHANServerRuntime
		PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(IDHANServerRuntime
		PUBLIC IDHANRuntime
		PRIVATE Folly::folly_executors_cpu_thread_pool_executor
		Folly::folly_executors_thread_factory_named_thread_factory
		Folly::folly_coro_task)
