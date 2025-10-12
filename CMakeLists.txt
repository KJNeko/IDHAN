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

option(BUILD_IDHAN_DOCS "Builds the API and source docs" ON)
option(BUILD_IDHAN_WEBUI "Builds the WASM WebUI for IDHAN server" ON)
option(BUILD_IDHAN_SERVER "Builds the server" ON)

option(BUILD_IDHAN_TESTS "Builds all tests relating to IDHAN" OFF)

option(BUILD_IDHAN_CLIENT "Builds the shared library for the IDHANClient" OFF)

option(BUILD_IDHAN_TOOLS "Builds all tools" OFF)
option(BUILD_HYDRUS_IMPORTER "Builds the hydrus importer" OFF)

if (BUILD_HYDRUS_IMPORTER)
    set(BUILD_IDHAN_CLIENT ON)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#IDHANServer/IDHANClient
include(postgres)
include(toml)

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
        message(FATAL_ERROR "You must define QT_WASM during cmake config")
    endif ()

    set(EMSDK ${QT_WASM}/emsdk)
    set(EMSDK_TOOLCHAIN)

    find_program(QTCMAKE_EXECUTABLE qt-cmake
            PATHS "${QT_WASM}/bin"
            NO_DEFAULT_PATH)

    if (NOT QTCMAKE_EXECUTABLE)
        message(FATAL_ERROR "qt-cmake not found for wasm")
    endif ()

    set(WEBUI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/IDHANWebUI)
    set(WEBUI_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/IDHANWebUI)

    # In order for this to work you need to set the env variables in /opt/qt6-wasm/qtwasm_env.sh
    # The following is a copy of that file
    #    #/opt/qt6-wasm/emsdk/emsdk activate 3.1.70
    #
    #    # load emsdk environment
    #    # EM_CACHE issue: https://github.com/emscripten-core/emsdk/issues/535
    #    EM_CACHE_PATH=~/.cache/qt6-wasm
    #    export EM_CACHE=$EM_CACHE_PATH
    #    source /opt/qt6-wasm/emsdk/emsdk_env.sh
    #    export EM_CACHE=$EM_CACHE_PATH
    #    echo "EM_CACHE (override) = $EM_CACHE"
    #
    #    # Export Qt6 WebAssembly bin Path
    #    echo $PATH | grep -w "/opt/qt6-wasm/bin" > /dev/null
    #    if [ $? -ne 0 ]; then
    #    export PATH=/opt/qt6-wasm/bin:$PATH
    #    fi


    ExternalProject_Add(
            IDHANWebUI_WASM
            SOURCE_DIR ${WEBUI_SOURCE_DIR}
            BINARY_DIR ${WEBUI_BUILD_DIR}
            CONFIGURE_COMMAND ${CMAKE_COMMAND}
            -S ${WEBUI_SOURCE_DIR}
            -B ${WEBUI_BUILD_DIR}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DWASM_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/bin/static
            BUILD_COMMAND ${CMAKE_COMMAND} --build ${WEBUI_BUILD_DIR}
            INSTALL_COMMAND "" # Needed or this errors
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