AddFGLLibrary(IDHANModules OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)

#pkg_check_modules(VIPS REQUIRED vips)
pkg_check_modules(VIPS REQUIRED vips-cpp)
find_package(Jsoncpp REQUIRED)

target_link_libraries(IDHANModules PUBLIC IDHAN ${VIPS_LIBRARIES} Jsoncpp_lib)
# SYSTEM: libvips headers should not be checked against our strict warning set.
target_include_directories(IDHANModules SYSTEM PUBLIC ${VIPS_INCLUDE_DIRS})

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

set(IDHAN_PREMADE_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/premade/common)

function(AddPremadeModule NAME SRC_DIR)
	AddFGLModule(${NAME} ${SRC_DIR})

	target_link_libraries(${NAME} PUBLIC IDHANModules IDHAN spdlog::spdlog ${VIPS_LIBRARIES})
	target_include_directories(${NAME} SYSTEM PRIVATE ${VIPS_INCLUDE_DIRS})
	target_include_directories(${NAME} PRIVATE ${IDHAN_PREMADE_COMMON_DIR})

	set_target_properties(${NAME} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
			LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
			ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules")
endfunction()

AddPremadeModule(IDHANVips ${CMAKE_CURRENT_SOURCE_DIR}/premade/vips)
AddPremadeModule(IDHANFFmpeg ${CMAKE_CURRENT_SOURCE_DIR}/premade/ffmpeg)
AddPremadeModule(IDHANPsd ${CMAKE_CURRENT_SOURCE_DIR}/premade/psd)
AddPremadeModule(IDHANArchive ${CMAKE_CURRENT_SOURCE_DIR}/premade/archives)
AddPremadeModule(IDHANUgoira ${CMAKE_CURRENT_SOURCE_DIR}/premade/ugoira)

pkg_check_modules(ONNXRUNTIME IMPORTED_TARGET libonnxruntime)

if (ONNXRUNTIME_FOUND)
	AddPremadeModule(IDHANEmbedding ${CMAKE_CURRENT_SOURCE_DIR}/premade/embedding)
	target_link_libraries(IDHANEmbedding PRIVATE PkgConfig::ONNXRUNTIME)
	message(STATUS "onnxruntime ${ONNXRUNTIME_VERSION} found, building IDHANEmbedding")
else ()
	message(STATUS "onnxruntime not found, skipping IDHANEmbedding (image embeddings unavailable)")
endif ()

target_link_libraries(IDHANFFmpeg PRIVATE PkgConfig::FFMPEG)
target_link_libraries(IDHANArchive PRIVATE LibArchive::LibArchive chardet)

# Convenience target so consumers can depend on "all premade modules" without naming each one.
add_custom_target(IDHANPremadeModules)
add_dependencies(IDHANPremadeModules IDHANVips IDHANFFmpeg IDHANPsd IDHANArchive IDHANUgoira)

if (ONNXRUNTIME_FOUND)
	add_dependencies(IDHANPremadeModules IDHANEmbedding)
endif ()
