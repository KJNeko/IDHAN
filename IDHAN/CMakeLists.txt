

file(GLOB_RECURSE LIB_IDHAN_SOURCES "src/*.cpp")
file(GLOB_RECURSE LIB_IDHAN_HEADERS "include/*.hpp")

add_library(IDHAN SHARED ${LIB_IDHAN_SOURCES} ${LIB_IDHAN_HEADERS})

target_include_directories(IDHAN PRIVATE src)
target_include_directories(IDHAN PUBLIC include)







