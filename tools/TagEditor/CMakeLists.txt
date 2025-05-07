
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Widgets)
AddFGLExecutable(TagEditor ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ui")
target_sources(TagEditor PRIVATE ${UI_FILES})

target_link_libraries(TagEditor PUBLIC Qt6::Core Qt6::Widgets Qt6::Concurrent IDHANClient)
