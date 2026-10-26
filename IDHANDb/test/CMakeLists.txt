find_package(Catch2 3 REQUIRED)

include(Catch)

AddFGLExecutable(IDHANDbTests ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(IDHANDbTests PRIVATE Catch2::Catch2 IDHANDb
		Folly::folly_coro_blocking_wait Folly::folly_coro_collect
		Folly::folly_io_async_scoped_event_base_thread Folly::folly_init_init)
catch_discover_tests(IDHANDbTests)
