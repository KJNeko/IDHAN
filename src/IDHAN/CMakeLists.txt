

file(GLOB_RECURSE LIB_IDHAN_SOURCES "src/*.cpp")
file(GLOB_RECURSE LIB_IDHAN_HEADERS "include/*.hpp")

add_library(IDHAN SHARED ${LIB_IDHAN_SOURCES} ${LIB_IDHAN_HEADERS})

target_include_directories(IDHAN PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(IDHAN PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)





