AddFGLExecutable(IDHANParameterTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANParameterTests PRIVATE
		${CMAKE_SOURCE_DIR}/IDHANServer/src/httpapi/jsonBody.cpp)
target_include_directories(IDHANParameterTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANParameterTests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHANDb Folly::folly spdlog::spdlog)
catch_discover_tests(IDHANParameterTests)
