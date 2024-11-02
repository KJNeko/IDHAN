

AddFGLExecutable(IDHANServer ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(IDHANServer PUBLIC spdlog fmt drogon)
target_link_libraries(IDHANServer PRIVATE pqxx sqlite3 Qt6::Core)
target_link_libraries(IDHANServer PRIVATE IDHAN)
target_include_directories(IDHANServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (DEFINED ALLOW_TABLE_DESTRUCTION AND ALLOW_TABLE_DESTRUCTION)
	target_compile_definitions(IDHANServer PUBLIC ALLOW_TABLE_DESTRUCTION=1)
endif ()