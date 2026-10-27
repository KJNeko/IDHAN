set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Widgets)
find_package(ZLIB REQUIRED)
AddFGLExecutable(HydrusImporter ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(HydrusImporter PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network IDHANClient sqlite3 spdlog::spdlog Qt6::Widgets ZLIB::ZLIB PTRCore)

file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/**.ui)

target_sources(HydrusImporter PRIVATE ${UI_FILES})

target_compile_options(HydrusImporter PRIVATE "-Wno-mismatched-tags")

target_compile_definitions(HydrusImporter PRIVATE $<$<CONFIG:Debug>:IDHAN_TRACK_CORO_FRAMES>)

if (WIN32)
	# Qt6::Core supplies the entry-point library for Windows GUI executables.
	set_target_properties(HydrusImporter PROPERTIES WIN32_EXECUTABLE TRUE)
endif ()

if (DEFINED IMPORTER_TESTS AND IMPORTER_TESTS EQUAL 1)
	target_compile_definitions(HydrusImporter PUBLIC IMPORTER_TESTS=1)
endif ()

if (DEFINED IMPORTER_PROCESS_PTR_DEFAULT AND IMPORTER_PROCESS_PTR_DEFAULT EQUAL 1)
	target_compile_definitions(HydrusImporter PUBLIC IMPORTER_PROCESS_PTR_DEFAULT=1)
endif ()

option(IDHAN_PACKAGE_HYDRUS_IMPORTER "Bundle the Hydrus Importer runtime for releases" OFF)

if (IDHAN_PACKAGE_HYDRUS_IMPORTER)
	find_package(Qt6 6.10 REQUIRED COMPONENTS Core Network Concurrent Widgets)
	if (WIN32)
		install(TARGETS HydrusImporter IDHANClient RUNTIME DESTINATION . COMPONENT HydrusImporter)
		get_filename_component(IMPORTER_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
		configure_file(packaging/deploy-windows.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/deploy-windows.cmake @ONLY)
		file(READ ${CMAKE_CURRENT_BINARY_DIR}/deploy-windows.cmake IMPORTER_DEPLOY_CONTENT)
		qt6_generate_deploy_script(TARGET HydrusImporter OUTPUT_SCRIPT IMPORTER_DEPLOY_SCRIPT
				CONTENT "${IMPORTER_DEPLOY_CONTENT}")
		install(SCRIPT ${IMPORTER_DEPLOY_SCRIPT} COMPONENT HydrusImporter)
		return()
	endif ()

	if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		message(FATAL_ERROR "Hydrus Importer release packaging supports Linux and Windows")
	endif ()

	include(GNUInstallDirs)
	install(TARGETS HydrusImporter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT HydrusImporter)
	# Bundle Qt, plugins, IDHANClient and their libraries, leaving glibc and its loader to the host.
	qt6_generate_deploy_script(
			TARGET HydrusImporter
			OUTPUT_SCRIPT IMPORTER_DEPLOY_SCRIPT
			CONTENT [[
qt6_deploy_runtime_dependencies(
	EXECUTABLE "$<TARGET_FILE:HydrusImporter>"
	GENERATE_QT_CONF
	NO_TRANSLATIONS
	INCLUDE_PLUGINS qxcb qwayland qwayland-egl qwayland-generic qoffscreen
	# Match glibc shared objects (for example libc.so.6) and the loader ld-linux-x86-64.so.2.
	POST_EXCLUDE_REGEXES "/lib(c|m|pthread|dl|rt|resolv|util|nss_[^/]*)[.]so[.]" "/ld-linux[^/]*[.]so[.]")
]])
	install(SCRIPT ${IMPORTER_DEPLOY_SCRIPT} COMPONENT HydrusImporter)
	configure_file(packaging/launch.sh.in ${CMAKE_CURRENT_BINARY_DIR}/HydrusImporter-launcher @ONLY)
	install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/HydrusImporter-launcher
			DESTINATION . RENAME HydrusImporter COMPONENT HydrusImporter)
endif ()
