

file(GLOB_RECURSE LIB_IDHAN_SERVER_SOURCES "src/**.cpp")
file(GLOB_RECURSE LIB_IDHAN_SERVER_HEADERS "include/**.hpp")

add_library(IDHANServer SHARED ${LIB_IDHAN_SERVER_SOURCES} ${LIB_IDHAN_SERVER_HEADERS})

target_link_libraries(IDHANServer PRIVATE IDHAN OpenSSL::SSL OpenSSL::Crypto asio spdlog)

target_include_directories(IDHANServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(IDHANServer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)




