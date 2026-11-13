AddFGLExecutable(IDHANModuleTests ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(IDHANModuleTests PRIVATE Catch2::Catch2WithMain IDHANModuleIPC IDHANModules IDHAN)
target_compile_definitions(IDHANModuleTests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# psd.cpp is compiled in directly: openWholeFile lives in the IDHANPsd module, which is a loadable
# module rather than a library anything can link.
target_sources(IDHANModuleTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANModules/premade/psd/psd.cpp)
target_include_directories(IDHANModuleTests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANModules/premade/psd)
add_dependencies(IDHANModuleTests IDHANVips)
add_test(NAME IDHANModuleTests COMMAND IDHANModuleTests)
