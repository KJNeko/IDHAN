AddFGLExecutable(IDHANImporterProgressTests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANImporterProgressTests PRIVATE
		${CMAKE_SOURCE_DIR}/tools/HydrusImporter/src/gui/hydrus/ImportProgress.cpp)
target_include_directories(IDHANImporterProgressTests PRIVATE ${CMAKE_SOURCE_DIR}/tools/HydrusImporter/src)
target_link_libraries(IDHANImporterProgressTests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(IDHANImporterProgressTests)
