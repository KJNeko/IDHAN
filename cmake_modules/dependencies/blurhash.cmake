add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/blurhash)
set_target_properties(BlurhashCXX PROPERTIES COMPILE_FLAGS ${FGL_FLAGS})