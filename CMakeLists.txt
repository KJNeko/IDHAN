# /CMakeLists.txt

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_STATIC_LIBS_ONLY ON)
set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

cmake_minimum_required(VERSION 3.28)
project(IDHAN LANGUAGES CXX)

set(IDHAN_DEPENDENCIES ${CMAKE_CURRENT_SOURCE_DIR}/dependencies)

enable_testing()

# Includes fgl_cmake_modules (defines AddFGLLibrary/AddFGLExecutable/AddFGLModule
# and the fgl::flags target). This configures the FGL build helpers on include;
# there is no separate PreSetup()/PostSetup() step.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libFGL)

# Find-modules (Finddrogon.cmake, postgres.cmake, ...) and config packages live
# under dependencies/.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies")
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_LIST_DIR}/dependencies")

option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)
option(BUILD_IDHAN_WEBUI "Builds the WASM WebUI for IDHAN server" OFF)
option(BUILD_IDHAN_SERVER "Builds the server" ON)

option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" OFF)

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" ON)
option(IDHAN_DISABLE_API_AUTH "Disable the API access key checks" OFF)

option(IDHAN_ENABLE_TRACE "Enable trace log level" ON)

option(BUILD_IDHAN_TOOLS "Builds all tools" ON)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" ON)

if (BUILD_HYDRUS_IMPORTER EQUAL 1)
	set(BUILD_IDHAN_CLIENT ON)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(IDHAN_ENABLE_TRACE OFF)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#IDHANServer/IDHANClient
include(postgres)
include(toml)

find_package(spdlog REQUIRED)
find_package(Jsoncpp REQUIRED)

add_subdirectory(IDHAN)
if (BUILD_IDHAN_SERVER)
	add_subdirectory(IDHANMigration)
	add_subdirectory(IDHANModules)
	add_subdirectory(IDHANServer)
endif ()
if (BUILD_IDHAN_CLIENT)
	add_subdirectory(IDHANClient)
endif ()

if (BUILD_HYDRUS_IMPORTER OR BUILD_IDHAN_TOOLS)
	#HydrusImporter
	include(sqlite3)
	add_subdirectory(tools/HydrusImporter)
endif ()

if (BUILD_IDHAN_DOCS)
	add_subdirectory(docs)
endif ()

if (BUILD_IDHAN_WEBUI)
	include(ExternalProject)

	find_program(EMPP_EXECUTABLE em++
			HINTS
			"/usr/lib/emscripten")

	if (NOT EMPP_EXECUTABLE)
		message(FATAL_ERROR "em++ not found. Emscripten SDK must be installed to build the WebUI. "
				"Set -DBUILD_IDHAN_WEBUI=OFF to skip the WebUI.")
	endif ()

	if (NOT DEFINED QT_WASM)
		if (DEFINED ENV{QT_WASM})
			set(QT_WASM $ENV{QT_WASM})
		elseif (DEFINED ENV{QT_WASM_PATH})
			set(QT_WASM $ENV{QT_WASM_PATH})
		endif ()
	endif ()

	if (NOT DEFINED QT_HOST_PATH)
		if (DEFINED ENV{QT_HOST_PATH})
			set(QT_HOST_PATH $ENV{QT_HOST_PATH})
		endif ()
	endif ()

	set(WEBUI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/IDHANWebUI)
	set(WEBUI_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/IDHANWebUI)

	set(IDHAN_WEBUI_CONFIGURE_ARGS
			-S ${WEBUI_SOURCE_DIR} -B ${WEBUI_BUILD_DIR}
			-G${CMAKE_GENERATOR}
			-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
			-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
			-DWASM_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/bin/static
			-DCMAKE_CXX_STANDARD=23)

	if (QT_HOST_PATH)
		list(APPEND IDHAN_WEBUI_CONFIGURE_ARGS -DQT_HOST_PATH=${QT_HOST_PATH})
	endif ()

	if (DEFINED IDHAN_WASM_TOOLCHAIN_FILE)
		list(APPEND IDHAN_WEBUI_CONFIGURE_ARGS
				-DCMAKE_TOOLCHAIN_FILE=${IDHAN_WASM_TOOLCHAIN_FILE})
		set(WEBUI_CONFIGURE_COMMAND ${CMAKE_COMMAND} ${IDHAN_WEBUI_CONFIGURE_ARGS})
	else ()
		find_program(QTCMAKE_EXECUTABLE qt-cmake
				HINTS "${QT_WASM}/bin" NO_DEFAULT_PATH)

		if (NOT QTCMAKE_EXECUTABLE)
			if (DEFINED QT_WASM)
				message(FATAL_ERROR "qt-cmake could not be found at ${QT_WASM}/bin")
			else ()
				message(FATAL_ERROR "qt-cmake not found. Define QT_WASM or set env variable.")
			endif ()
		endif ()

		if (NOT QT_WASM)
			get_filename_component(QT_WASM_BIN_DIR "${QTCMAKE_EXECUTABLE}" DIRECTORY)
			get_filename_component(QT_WASM "${QT_WASM_BIN_DIR}" DIRECTORY)
		endif ()

		set(WEBUI_CONFIGURE_COMMAND ${QTCMAKE_EXECUTABLE} ${IDHAN_WEBUI_CONFIGURE_ARGS})
	endif ()

	ExternalProject_Add(
			IDHANWebUI_WASM
			SOURCE_DIR ${WEBUI_SOURCE_DIR}
			BINARY_DIR ${WEBUI_BUILD_DIR}
			CONFIGURE_COMMAND ${WEBUI_CONFIGURE_COMMAND}
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