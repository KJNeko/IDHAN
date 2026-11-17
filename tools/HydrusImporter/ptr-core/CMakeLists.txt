
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

AddFGLLibrary(PTRCore STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(PTRCore PUBLIC IDHAN)
target_link_libraries(PTRCore PRIVATE ZLIB::ZLIB spdlog::spdlog Threads::Threads)

if (WIN32)
	target_compile_definitions(PTRCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

if (BUILD_PTR_CORE_TESTS)
	AddFGLExecutable(PTRCoreTests ${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_link_libraries(PTRCoreTests PRIVATE PTRCore Catch2::Catch2WithMain)
	add_test(NAME PTRCoreTests COMMAND PTRCoreTests)
endif ()
