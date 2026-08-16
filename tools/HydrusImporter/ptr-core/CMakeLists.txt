
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

AddFGLLibrary(PTRCore STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(PTRCore PUBLIC IDHAN)
target_link_libraries(PTRCore PRIVATE ZLIB::ZLIB spdlog::spdlog Threads::Threads)
