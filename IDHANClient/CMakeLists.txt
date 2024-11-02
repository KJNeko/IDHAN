

AddFGLLibrary(IDHANClient SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(IDHANClient PUBLIC IDHAN)
target_link_libraries(IDHANClient PRIVATE Qt6::Core)