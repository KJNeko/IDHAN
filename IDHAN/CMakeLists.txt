
AddFGLLibrary(IDHAN OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(HYDRUS_CONSTANTS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus_constants_gen.hpp)
set(HYDRUS_CLIENT_CONSTANTS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus_client_constants_gen.hpp)
set(HYDRUS_SERIALISABLE_CONSTANTS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus_serialisable_constants_gen.hpp)

set(HYDRUS_REPO_PATH ${CMAKE_SOURCE_DIR}/3rd-party/hydrus)
set(HYDRUS_SOURCE_FILE ${HYDRUS_REPO_PATH}/hydrus/core/HydrusConstants.py)
set(HYDRUS_CLIENT_SOURCE_FILE ${HYDRUS_REPO_PATH}/hydrus/client/ClientConstants.py)
set(HYDRUS_SERIALISABLE_SOURCE_FILE ${HYDRUS_REPO_PATH}/hydrus/core/HydrusSerialisable.py)
set(HYDRUS_REPO "https://github.com/hydrusnetwork/hydrus.git")

if (NOT EXISTS ${HYDRUS_SOURCE_FILE})
    message("Hydrus source file not found. Cloning repository...")
    execute_process(
            COMMAND git clone ${HYDRUS_REPO} ${HYDRUS_REPO_PATH}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/3rd-party
    )
endif ()

if (IDHAN_USE_STD_FORMAT)

    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
    #include <format>
    int main() {
        std::format(\"Test {}\", 42);
        return 0;
    }
    " HAVE_STD_FORMAT)

    if (HAVE_STD_FORMAT)
        target_compile_definitions(IDHAN PUBLIC IDHAN_USE_STD_FORMAT=1)
    else ()
        target_compile_definitions(IDHAN PUBLIC IDHAN_USE_STD_FORMAT=0)
    endif ()

endif ()

if (NOT EXISTS ${HYDRUS_SOURCE_FILE})
    message(FATAL_ERROR "Failed to obtain Hydrus source file")
endif ()

add_custom_command(
        OUTPUT ${HYDRUS_CONSTANTS_FILE}
        DEPENDS ${HYDRUS_SOURCE_FILE}
        COMMAND ${CMAKE_COMMAND} -DHYDRUS_DIR=${HYDRUS_REPO_PATH} -DHYDRUS_CONSTANTS_FILE=${HYDRUS_SOURCE_FILE} -DOUT_TARGET=${HYDRUS_CONSTANTS_FILE} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateHydrusConstants.cmake"
        COMMENT "Generating hydrus constants file")
target_sources(IDHAN PUBLIC ${HYDRUS_CONSTANTS_FILE})

add_custom_command(
        OUTPUT ${HYDRUS_CLIENT_CONSTANTS_FILE}
        DEPENDS ${HYDRUS_CLIENT_SOURCE_FILE}
        COMMAND ${CMAKE_COMMAND} -DHYDRUS_DIR=${HYDRUS_REPO_PATH} -DHYDRUS_CONSTANTS_FILE=${HYDRUS_CLIENT_SOURCE_FILE} -DOUT_TARGET=${HYDRUS_CLIENT_CONSTANTS_FILE} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateHydrusConstants.cmake"
        COMMENT "Generating hydrus client constants file")
target_sources(IDHAN PUBLIC ${HYDRUS_CLIENT_CONSTANTS_FILE})

add_custom_command(
        OUTPUT ${HYDRUS_SERIALISABLE_CONSTANTS_FILE}
        DEPENDS ${HYDRUS_SERIALISABLE_SOURCE_FILE}
        COMMAND ${CMAKE_COMMAND} -DHYDRUS_DIR=${HYDRUS_REPO_PATH} -DHYDRUS_CONSTANTS_FILE=${HYDRUS_SERIALISABLE_SOURCE_FILE} -DOUT_TARGET=${HYDRUS_SERIALISABLE_CONSTANTS_FILE} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateHydrusConstants.cmake"
        COMMENT "Generating Hydrus serailisable file")
target_sources(IDHAN PUBLIC ${HYDRUS_SERIALISABLE_CONSTANTS_FILE})

target_link_libraries(IDHAN libFGL)