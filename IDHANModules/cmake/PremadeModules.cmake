set(IDHAN_PREMADE_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/premade/common)

function(idhan_add_premade_module NAME SRC_DIR)
	AddFGLModule(${NAME} ${SRC_DIR})

	target_link_libraries(${NAME} PUBLIC IDHANModules IDHAN spdlog::spdlog PkgConfig::VIPS)
	target_include_directories(${NAME} PRIVATE ${IDHAN_PREMADE_COMMON_DIR})

	set_target_properties(${NAME} PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
			LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules"
			ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/modules")
endfunction()

idhan_add_premade_module(IDHANVips ${CMAKE_CURRENT_SOURCE_DIR}/premade/vips)
idhan_add_premade_module(IDHANFFmpeg ${CMAKE_CURRENT_SOURCE_DIR}/premade/ffmpeg)
idhan_add_premade_module(IDHANPsd ${CMAKE_CURRENT_SOURCE_DIR}/premade/psd)
idhan_add_premade_module(IDHANArchive ${CMAKE_CURRENT_SOURCE_DIR}/premade/archives)
idhan_add_premade_module(IDHANUgoira ${CMAKE_CURRENT_SOURCE_DIR}/premade/ugoira)

pkg_check_modules(ONNXRUNTIME IMPORTED_TARGET libonnxruntime)

if (ONNXRUNTIME_FOUND)
	idhan_add_premade_module(IDHANEmbedding ${CMAKE_CURRENT_SOURCE_DIR}/premade/embedding)
	target_link_libraries(IDHANEmbedding PRIVATE PkgConfig::ONNXRUNTIME)
	message(STATUS "onnxruntime ${ONNXRUNTIME_VERSION} found, building IDHANEmbedding")
else ()
	message(STATUS "onnxruntime not found, skipping IDHANEmbedding (image embeddings unavailable)")
endif ()

target_link_libraries(IDHANFFmpeg PRIVATE PkgConfig::FFMPEG)
target_link_libraries(IDHANArchive PRIVATE LibArchive::LibArchive chardet)

add_custom_target(IDHANPremadeModules)
add_dependencies(IDHANPremadeModules IDHANVips IDHANFFmpeg IDHANPsd IDHANArchive IDHANUgoira)

if (ONNXRUNTIME_FOUND)
	add_dependencies(IDHANPremadeModules IDHANEmbedding)
endif ()
