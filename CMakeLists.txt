# /CMakeLists.txt

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_minimum_required(VERSION 3.28)
project(IDHAN LANGUAGES CXX)

enable_testing()

# Includes fgl_cmake_modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libFGL)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/dependencies")

PreSetup()

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" ON)
option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" ON)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)
option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)
option(BUILD_IDHAN_WEBUI "Builds the WASM WebUI for IDHAN server" ON)

if (BUILD_HYDRUS_IMPORTER)
	set(BUILD_IDHAN_CLIENT ON)
endif ()

if (BUILD_IDHAN_WEBUI)
	set(BUILD_IDHAN_SERVER ON)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#IDHANServer/IDHANClient
include(postgres)
include(toml)

add_subdirectory(IDHAN)
add_subdirectory(IDHANMigration)
add_subdirectory(IDHANModules)
add_subdirectory(IDHANServer)
add_subdirectory(IDHANClient)
add_subdirectory(tools/TagEditor)
add_subdirectory(docs)


if (BUILD_IDHAN_WEBUI)
	include(ExternalProject)

	if (NOT DEFINED QT_WASM)
		message(FATAL_ERROR "You must define QT_WASM during cmake config")
	endif ()

	set(EMSDK ${QT_WASM}/emsdk)

	find_program(QTCMAKE_EXECUTABLE qt-cmake
			PATHS "${QT_WASM}/bin"
			NO_DEFAULT_PATH)

	set(WEBUI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/IDHANWebUI)
	set(WEBUI_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/IDHANWebUI)

	ExternalProject_Add(
			IDHANWebUI_WASM
			SOURCE_DIR ${WEBUI_SOURCE_DIR}
			BINARY_DIR ${WEBUI_BUILD_DIR}
			CONFIGURE_COMMAND ${QTCMAKE_EXECUTABLE}
			-S ${WEBUI_SOURCE_DIR}
			-B ${WEBUI_BUILD_DIR}
			-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
			-DWASM_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/bin/static
			BUILD_COMMAND ${CMAKE_COMMAND} --build ${WEBUI_BUILD_DIR}
			INSTALL_COMMAND ""
			BUILD_ALWAYS YES
	)

	add_custom_target(IDHANWebUIFiles DEPENDS ${WEBUI_FILES_OUTPUT})
	add_dependencies(IDHANServer IDHANWebUI_WASM IDHANWebUIFiles)
endif ()

if (BUILD_IDHAN_TESTS)
	#IDHANTests
	add_subdirectory(tests)
endif ()

if (BUILD_HYDRUS_IMPORTER)
	#HydrusImporter
	include(sqlite3)
	add_subdirectory(HydrusImporter)
endif ()

PostSetup()