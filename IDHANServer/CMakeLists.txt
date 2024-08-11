

AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(IDHANServer PUBLIC spdlog fmt drogon)
target_link_libraries(IDHANServer PRIVATE pqxx sqlite3 Qt6::Core)
target_include_directories(IDHANServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)