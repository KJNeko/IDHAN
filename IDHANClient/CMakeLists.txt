
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)

AddFGLLibrary(IDHANClient SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(IDHANClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/idhan)
target_link_libraries(IDHANClient PUBLIC IDHAN)
target_link_libraries(IDHANClient PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent spdlog::spdlog_header_only)