AddFGLLibrary(IDHANModules SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

include(ExternalProject)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED vips)

AddFGLModule(IDHANPremadeModules ${CMAKE_CURRENT_SOURCE_DIR}/premade)
target_link_libraries(IDHANPremadeModules PUBLIC IDHANModules IDHAN ${VIPS_LIBRARIES} spdlog)
target_include_directories(IDHANPremadeModules PRIVATE ${VIPS_INCLUDE_DIRS})

set_target_properties(IDHANPremadeModules PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules")