AddFGLLibrary(IDHANModules OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)

# ImageVipsMetadata & ImageVipsThumbnailer
#pkg_check_modules(VIPS REQUIRED vips)
pkg_check_modules(VIPS REQUIRED vips-cpp)

target_link_libraries(IDHANModules PUBLIC IDHAN ${VIPS_LIBRARIES})
target_include_directories(IDHANModules PUBLIC ${VIPS_INCLUDE_DIRS})

include(ExternalProject)

find_package(libarchive REQUIRED)

# VideoMetadata & VideoThumbnailer
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
		libavcodec
		libavformat
		libavutil
		libswscale)

AddFGLModule(IDHANPremadeModules ${CMAKE_CURRENT_SOURCE_DIR}/premade)
target_link_libraries(IDHANPremadeModules PUBLIC IDHANModules IDHAN spdlog)

# Vips
target_link_libraries(IDHANPremadeModules PUBLIC ${VIPS_LIBRARIES})
target_include_directories(IDHANPremadeModules PRIVATE ${VIPS_INCLUDE_DIRS})
#target_link_libraries(IDHANPremadeModules PRIVATE PkgConfig::VIPS)

target_link_libraries(IDHANPremadeModules PRIVATE chardet)

find_package(Jsoncpp REQUIRED)

target_link_libraries(IDHANModules PUBLIC Jsoncpp_lib)

# Video (FFMPEG)
target_link_libraries(IDHANPremadeModules PRIVATE PkgConfig::FFMPEG)

find_package(Chardet REQUIRED)


set_target_properties(IDHANPremadeModules PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
		ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules")