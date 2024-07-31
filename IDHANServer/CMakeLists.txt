

AddFGLLibrary(LibIDHANServer OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LibIDHANServer PUBLIC spdlog fmt drogon)
target_link_libraries(LibIDHANServer PRIVATE pqxx sqlite3)
target_include_directories(LibIDHANServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(IDHANServer main.cpp)
SetFGLFlags(IDHANServer)
target_link_libraries(IDHANServer LibIDHANServer)
