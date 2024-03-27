

file(GLOB_RECURSE LIB_IDHAN_CLIENT_SOURCES "src/**.cpp")
file(GLOB_RECURSE LIB_IDHAN_CLIENT_HEADERS "include/**.hpp")

add_library(IDHANClient SHARED ${LIB_IDHAN_CLIENT_SOURCES} ${LIB_IDHAN_CLIENT_HEADERS})

target_link_libraries(IDHANClient PUBLIC IDHAN)
target_link_libraries(IDHANClient PUBLIC OpenSSL::SSL OpenSSL::Crypto asio spdlog)

target_include_directories(IDHANClient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(IDHANClient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)




