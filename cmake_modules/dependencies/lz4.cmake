set(LZ4_DIR ${CMAKE_SOURCE_DIR}/dependencies/lz4/lib)

file(GLOB_RECURSE LZ4_SOURCES ${LZ4_DIR}/*.c)

add_library(lz4 STATIC ${LZ4_SOURCES})
target_include_directories(lz4 PUBLIC ${LZ4_DIR})

if (WIN32)
	#target_compile_definitions(${CMAKE_SOURCE_DIR}/dependencies/lz4 PRIVATE UNICODE=1)
	target_compile_definitions(lz4 PRIVATE LZ4_DEBUG=0)
endif ()