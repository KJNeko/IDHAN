
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)

AddFGLLibrary(IDHANClient SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
include(GenerateExportHeader)
# Explicit API exports avoid MinGW auto-exporting STL templates during LTO.
generate_export_header(IDHANClient
		EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/idhan/IDHANClientExport.hpp
		EXPORT_MACRO_NAME IDHAN_CLIENT_EXPORT)
target_include_directories(IDHANClient PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(IDHANClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/idhan)
target_link_libraries(IDHANClient PUBLIC IDHAN)
target_link_libraries(IDHANClient PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent spdlog::spdlog)
