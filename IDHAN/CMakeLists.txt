
AddFGLLibrary(IDHAN OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(HYDRUS_CONSTANTS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus_constants_gen.hpp)
set(HYDRUS_CLIENT_CONSTANTS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus_client_constants_gen.hpp)
set(HYDRUS_SERIALISABLE_CONSTANTS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus_serialisable_constants_gen.hpp)

set(HYDRUS_REPO_PATH ${CMAKE_SOURCE_DIR}/3rd-party/hydrus)
set(HYDRUS_REPO "https://github.com/hydrusnetwork/hydrus.git")

if (NOT EXISTS ${HYDRUS_REPO_PATH})
	message("Hydrus source file not found. Cloning repository...")
	execute_process(
			COMMAND git clone ${HYDRUS_REPO} ${HYDRUS_REPO_PATH}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/3rd-party
	)
endif ()

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

list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/client/gui/pages/ClientGUIPagesCore.py)
list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/core/HydrusSerialisable.py)
list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/client/ClientConstants.py)
list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/core/HydrusConstants.py)

set(GENERATED_HEADERS "")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus)

foreach (HYDRUS_FILE ${HYDRUS_SCAN_FILES})
	get_filename_component(FILE_NAME ${HYDRUS_FILE} NAME_WE)
	set(OUTPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/include/hydrus/${FILE_NAME}_gen.hpp")
	list(APPEND GENERATED_HEADERS ${OUTPUT_FILE})

	add_custom_target(
			generate_${FILE_NAME}_constants ALL
			COMMAND ${CMAKE_COMMAND}
			-DHYDRUS_DIR=${HYDRUS_REPO_PATH}
			-DHYDRUS_CONSTANTS_FILE=${HYDRUS_FILE}
			-DOUT_TARGET=${OUTPUT_FILE}
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateHydrusConstants.cmake"
			COMMENT "Generating ${FILE_NAME} constants file"
	)

	add_dependencies(IDHAN generate_${FILE_NAME}_constants)
endforeach ()

target_sources(IDHAN PUBLIC ${GENERATED_HEADERS})

target_link_libraries(IDHAN libFGL)