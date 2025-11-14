AddFGLLibrary(IDHANModules OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(IDHANModules PUBLIC IDHAN)

include(ExternalProject)

find_package(PkgConfig REQUIRED)

# VideoMetadata & VideoThumbnailer
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
		libavcodec
		libavformat
		libavutil
		libswscale)

# ImageVipsMetadata & ImageVipsThumbnailer
pkg_check_modules(VIPS REQUIRED vips)

AddFGLModule(IDHANPremadeModules ${CMAKE_CURRENT_SOURCE_DIR}/premade)
target_link_libraries(IDHANPremadeModules PUBLIC IDHANModules IDHAN spdlog)

# Vips
target_link_libraries(IDHANPremadeModules PUBLIC ${VIPS_LIBRARIES})
target_include_directories(IDHANPremadeModules PRIVATE ${VIPS_INCLUDE_DIRS})
#target_link_libraries(IDHANPremadeModules PRIVATE PkgConfig::VIPS)

# Video (FFMPEG)
target_link_libraries(IDHANPremadeModules PRIVATE PkgConfig::FFMPEG)

set_target_properties(IDHANPremadeModules PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules")