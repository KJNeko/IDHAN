set(HYDRUS_REPO_PATH "${IDHAN_SOURCE_DIR}/3rd-party/hydrus")
if (NOT EXISTS "${HYDRUS_REPO_PATH}/hydrus/core/HydrusConstants.py")
	message(FATAL_ERROR "Hydrus sources are missing; run git submodule update --init --recursive")
endif ()

list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/client/gui/pages/ClientGUIPagesCore.py)
list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/core/HydrusSerialisable.py)
list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/client/ClientConstants.py)
list(APPEND HYDRUS_SCAN_FILES ${HYDRUS_REPO_PATH}/hydrus/core/HydrusConstants.py)

set(GENERATED_HEADERS "")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/hydrus)

set(COMMAND_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/GenerateHydrusConstants.cmake")

foreach (HYDRUS_FILE ${HYDRUS_SCAN_FILES})
	get_filename_component(FILE_NAME ${HYDRUS_FILE} NAME_WE)
	set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/hydrus/${FILE_NAME}_gen.hpp")
	list(APPEND GENERATED_HEADERS ${OUTPUT_FILE})

	add_custom_command(
			OUTPUT ${OUTPUT_FILE}
			COMMAND ${CMAKE_COMMAND}
			-DHYDRUS_DIR=${HYDRUS_REPO_PATH}
			-DHYDRUS_CONSTANTS_FILE=${HYDRUS_FILE}
			-DOUT_TARGET=${OUTPUT_FILE}
			-P ${COMMAND_FILE}
			DEPENDS ${HYDRUS_FILE} ${COMMAND_FILE}
			COMMENT "Generating ${FILE_NAME} constants file"
			VERBATIM
	)
endforeach ()

add_custom_target(IDHANHydrusConstants DEPENDS ${GENERATED_HEADERS})
add_dependencies(IDHAN IDHANHydrusConstants)
target_sources(IDHAN PRIVATE ${GENERATED_HEADERS})
# BEFORE also overrides generated headers left behind by older builds.
target_include_directories(IDHAN BEFORE PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")

