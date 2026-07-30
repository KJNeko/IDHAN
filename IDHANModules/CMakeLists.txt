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

# The IPC layer, shared by the server and the module runner. Deliberately knows nothing about either
# one: no drogon, no ModuleLoader, no premade module. It speaks the module ABI types (they are what
# crosses the wire) and nothing else.
#
# Everything platform-specific lives under ipc/src/linux. Modules are Linux-only for now; a Windows
# backend would add ipc/src/windows implementing the same Blob/Frame/UniqueFd headers (named pipe,
# CreateFileMapping section, DuplicateHandle) without any caller changing.
AddFGLLibrary(IDHANModuleIPC OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/ipc/src ${CMAKE_CURRENT_SOURCE_DIR}/ipc/include)
target_link_libraries(IDHANModuleIPC PUBLIC IDHANModules IDHAN Jsoncpp_lib)

# The worker process. One of these is spawned per module library; the server itself never dlopens a
# module again, so a leak or a crash in one is contained to the process hosting it.
AddFGLExecutable(IDHANModuleRunner ${CMAKE_CURRENT_SOURCE_DIR}/runner-src)
target_link_libraries(IDHANModuleRunner PRIVATE IDHANModuleIPC IDHANModules IDHAN spdlog::spdlog ${CMAKE_DL_LIBS})

# Headers shared by the premade libraries (the vips MIME map and the vips bring-up). Header-only on
# purpose: the libraries are separate dlopen units, so sharing compiled code between them would only
# duplicate it back into each .so.
set(IDHAN_PREMADE_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/premade/common)

# Each premade library is dlopened into its OWN worker process, so each one is a self-contained
# MODULE exporting getModulesFunc/init/deinit and running its own VIPS_INIT. Splitting them is what
# makes the isolation per-backend: a libvips leak cannot reach the FFmpeg worker.
#
# Every one of them links vips, even the FFmpeg one -- ThumbnailerModuleI::createThumbnailFile and
# ThumbnailInfo(VipsImagePtr&&) live in the shared IDHANModules object library and encode through it.
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

# Backend-specific dependencies. Keeping these off the shared helper is the point of the split:
# only the FFmpeg worker links FFmpeg, only the archive worker links libarchive.
target_link_libraries(IDHANFFmpeg PRIVATE PkgConfig::FFMPEG)
target_link_libraries(IDHANArchive PRIVATE LibArchive::LibArchive chardet)

# Convenience target so consumers can depend on "all premade modules" without naming each one.
add_custom_target(IDHANPremadeModules)
add_dependencies(IDHANPremadeModules IDHANVips IDHANFFmpeg IDHANPsd IDHANArchive)
