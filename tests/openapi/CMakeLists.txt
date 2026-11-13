AddFGLExecutable(IDHANOpenAPITests ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(IDHANOpenAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src)
target_link_libraries(IDHANOpenAPITests PRIVATE Catch2::Catch2WithMain IDHANHttp IDHANOpenAPIDocument)
target_compile_options(IDHANOpenAPITests PRIVATE
		$<$<CXX_COMPILER_ID:Clang>:-fconstexpr-steps=10000000>)
add_dependencies(IDHANOpenAPITests IDHANOpenAPI)
catch_discover_tests(IDHANOpenAPITests)
