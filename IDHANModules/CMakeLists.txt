AddFGLLibrary(IDHANModules OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)

pkg_check_modules(VIPS REQUIRED IMPORTED_TARGET vips-cpp)

target_link_libraries(IDHANModules PUBLIC IDHAN PkgConfig::VIPS Jsoncpp_lib)

find_package(LibArchive REQUIRED)
find_package(Chardet REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
		libavcodec
		libavformat
		libavutil
		libswscale)

AddFGLLibrary(IDHANModuleIPC OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/ipc/src ${CMAKE_CURRENT_SOURCE_DIR}/ipc/include)
target_link_libraries(IDHANModuleIPC PUBLIC IDHANModules IDHAN Jsoncpp_lib)

AddFGLExecutable(IDHANModuleRunner ${CMAKE_CURRENT_SOURCE_DIR}/runner-src)
target_link_libraries(IDHANModuleRunner PRIVATE IDHANModuleIPC IDHANModules IDHAN spdlog::spdlog ${CMAKE_DL_LIBS})

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PremadeModules.cmake")
