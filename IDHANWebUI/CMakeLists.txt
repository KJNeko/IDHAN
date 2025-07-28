cmake_minimum_required(VERSION 3.16)

project(IDHANWebUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

file(GLOB_RECURSE CPP_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/src/**.cpp)

qt_add_executable(IDHANWebUI ${CPP_SOURCES})

if (DEFINED WASM_OUT_DIR)
	set(OUT_DIR ${WASM_OUT_DIR})
else ()
	set(OUT_DIR ${CMAKE_BINARY_DIR})
endif ()

file(MAKE_DIRECTORY ${OUT_DIR})

if (EMSCRIPTEN)
	target_link_options(IDHANWebUI PUBLIC -sASYNCIFY -Os)
	set_target_properties(IDHANWebUI PROPERTIES QT_WASM_INITAL_MEMORY "64MB")

	# Set output directory for all WASM-related files
	set_target_properties(IDHANWebUI PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR}
			ARCHIVE_OUTPUT_DIRECTORY ${OUT_DIR}
			LIBRARY_OUTPUT_DIRECTORY ${OUT_DIR}
	)

	# For Qt WASM, also set the specific output directory
	set_target_properties(IDHANWebUI PROPERTIES
			QT_WASM_OUTPUT_DIRECTORY ${OUT_DIR}
	)
endif ()

target_link_libraries(IDHANWebUI PRIVATE Qt6::Widgets Qt6::Network)