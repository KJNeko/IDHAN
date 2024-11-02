

AddFGLExecutable(HydrusImporter ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(HydrusImporter PUBLIC Qt6::Core IDHANClient sqlite3)