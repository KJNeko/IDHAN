AddFGLExecutable(IDHANAPITests ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(IDHANAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src/filesystem/PathHelpers.cpp)
target_link_libraries(IDHANAPITests PRIVATE Catch2::Catch2WithMain libpqxx::pqxx CURL::libcurl IDHAN)
target_include_directories(IDHANAPITests PRIVATE ${CMAKE_SOURCE_DIR}/IDHANServer/src ${CMAKE_SOURCE_DIR}/IDHANDb/include ${IDHAN_TEST_SOURCE_DIR}/common ${CMAKE_CURRENT_SOURCE_DIR}/harness)
target_compile_definitions(IDHANAPITests PRIVATE IDHAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# the ugoira scenario refines a mime out of process, so the archive module has to sit beside the server
add_dependencies(IDHANAPITests IDHANServer IDHANModuleRunner IDHANArchive)
catch_discover_tests(IDHANAPITests)
