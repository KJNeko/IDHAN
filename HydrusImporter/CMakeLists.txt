find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)
AddFGLExecutable(HydrusImporter ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(HydrusImporter PUBLIC Qt6::Core Qt6::Concurrent IDHANClient sqlite3 spdlog)