set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Widgets)
AddFGLExecutable(HydrusImporter ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(HydrusImporter PUBLIC Qt6::Core Qt6::Concurrent IDHANClient sqlite3 spdlog Qt6::Widgets)

if (DEFINED IMPORTER_TESTS AND IMPORTER_TESTS EQUAL 1)
	target_compile_definitions(HydrusImporter PUBLIC IMPORTER_TESTS=1)
endif ()