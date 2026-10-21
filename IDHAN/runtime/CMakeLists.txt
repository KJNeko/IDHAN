add_library(IDHANRuntime STATIC DrainRegistry.cpp)
target_include_directories(IDHANRuntime PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../include)
target_link_libraries(IDHANRuntime PUBLIC Folly::folly_coro_task PRIVATE Folly::folly_coro_baton)
