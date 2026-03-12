# /CMakeLists.txt

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_STATIC_LIBS_ONLY ON)
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

cmake_minimum_required(VERSION 3.28)
project(IDHAN LANGUAGES CXX)

set(IDHAN_DEPENDENCIES ${CMAKE_CURRENT_SOURCE_DIR}/dependencies)

enable_testing()

# Includes fgl_cmake_modules
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libFGL)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/dependencies")

PreSetup()

option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)
option(BUILD_IDHAN_WEBUI "Builds the WASM WebUI for IDHAN server" OFF)
option(BUILD_IDHAN_SERVER "Builds the server" ON)

option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" OFF)

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" ON)

option(BUILD_IDHAN_TOOLS "Builds all tools" OFF)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)

if (BUILD_HYDRUS_IMPORTER)
	set(BUILD_IDHAN_CLIENT ON)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#IDHANServer/IDHANClient
include(postgres)
include(toml)

add_subdirectory(${IDHAN_DEPENDENCIES}/jsoncpp ${CMAKE_CURRENT_BINARY_DIR}/jsoncpp)

add_subdirectory(IDHAN)
if (BUILD_IDHAN_SERVER)
	add_subdirectory(IDHANMigration)
	add_subdirectory(IDHANModules)
	add_subdirectory(IDHANServer)
endif ()
if (BUILD_IDHAN_CLIENT)
	add_subdirectory(IDHANClient)
endif ()
if (BUILD_IDHAN_TOOLS)
	add_subdirectory(tools/TagEditor)
endif ()
if (BUILD_IDHAN_DOCS)
	add_subdirectory(docs)
endif ()

if (BUILD_IDHAN_WEBUI)
	include(ExternalProject)

	if (NOT DEFINED QT_WASM)
		if (DEFINED ENV{QT_WASM})
			set(QT_WASM $ENV{QT_WASM})
		elseif (DEFINED ENV{QT_WASM_PATH})
			set(QT_WASM $ENV{QT_WASM_PATH})
		endif ()
	endif ()

	find_program(QTCMAKE_EXECUTABLE qt-cmake
			HINTS
			"${QT_WASM}/bin"
			"/opt/qt6-wasm/bin"
			"/opt/qt/wasm_64/bin"
			"/usr/lib/qt6/bin"
			"/usr/lib/qt/bin"
			"/usr/lib/qt6/wasm-64/bin"
			"/usr/lib/qt6/wasm-emscripten/bin")

	if (NOT QTCMAKE_EXECUTABLE)
		message(FATAL_ERROR "qt-cmake not found for wasm. Please define QT_WASM during cmake config or set QT_WASM env variable.")
	endif ()

	if (NOT QT_WASM)
		get_filename_component(QT_WASM_BIN_DIR "${QTCMAKE_EXECUTABLE}" DIRECTORY)
		get_filename_component(QT_WASM "${QT_WASM_BIN_DIR}" DIRECTORY)
	endif ()

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
			-DCMAKE_CXX_STANDARD=23
			BUILD_COMMAND ${CMAKE_COMMAND} --build ${WEBUI_BUILD_DIR}
			INSTALL_COMMAND "" # Needed or this errors
			BUILD_ALWAYS YES
	)

	add_dependencies(IDHANServer IDHANWebUI_WASM)
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