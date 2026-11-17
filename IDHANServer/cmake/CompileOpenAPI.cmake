# Discover route headers at build time; all registration and schema work is C++
# constant evaluation over the generated include header.
if (NOT CMAKE_OBJCOPY)
	message(FATAL_ERROR "Exporting compiled OpenAPI requires an objcopy tool")
endif ()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(IDHAN_OPENAPI_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(IDHAN_GENERATED_ROUTES "${IDHAN_OPENAPI_DIRECTORY}/gen_routes.hpp")
set(IDHAN_ROUTE_GENERATOR "${CMAKE_CURRENT_LIST_DIR}/GenerateRoutes.cmake")
set(IDHAN_OPENAPI_EXPORTER "${CMAKE_CURRENT_LIST_DIR}/ExportCompiledOpenAPI.cmake")

file(GLOB_RECURSE IDHAN_ROUTE_INPUTS CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.hh" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hxx"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_custom_command(OUTPUT "${IDHAN_GENERATED_ROUTES}"
		COMMAND "${CMAKE_COMMAND}" "-DIDHAN_ROUTE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src"
		"-DIDHAN_ROUTE_OUTPUT=${IDHAN_GENERATED_ROUTES}" -P "${IDHAN_ROUTE_GENERATOR}"
		DEPENDS "${IDHAN_ROUTE_GENERATOR}" ${IDHAN_ROUTE_INPUTS}
		COMMENT "Discovering annotated route headers for gen_routes.hpp"
		VERBATIM)
add_custom_target(IDHANGeneratedRoutes DEPENDS "${IDHAN_GENERATED_ROUTES}")
add_dependencies(IDHANServer IDHANGeneratedRoutes)
target_include_directories(IDHANServer PRIVATE "${IDHAN_OPENAPI_DIRECTORY}")

# Compile the immutable document once and share the exact object with endpoint
# tests. No server or database is executed while compiling or exporting it.
set(IDHAN_OPENAPI_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/httpapi/openapi.cpp")
get_target_property(IDHAN_SERVER_SOURCES IDHANServer SOURCES)
list(REMOVE_ITEM IDHAN_SERVER_SOURCES "${IDHAN_OPENAPI_SOURCE}")
set_property(TARGET IDHANServer PROPERTY SOURCES "${IDHAN_SERVER_SOURCES}")
get_target_property(IDHAN_SERVER_LIBRARIES IDHANServer LINK_LIBRARIES)
add_library(IDHANOpenAPIDocument OBJECT "${IDHAN_OPENAPI_SOURCE}")
target_link_libraries(IDHANOpenAPIDocument PRIVATE ${IDHAN_SERVER_LIBRARIES})
target_include_directories(IDHANOpenAPIDocument PRIVATE "$<TARGET_PROPERTY:IDHANServer,INCLUDE_DIRECTORIES>")
target_compile_definitions(IDHANOpenAPIDocument PRIVATE "$<TARGET_PROPERTY:IDHANServer,COMPILE_DEFINITIONS>")
target_compile_options(IDHANOpenAPIDocument PRIVATE "$<TARGET_PROPERTY:IDHANServer,COMPILE_OPTIONS>")
# Export needs a native data section in the object; slim LTO objects contain
# only compiler IR. Keep this object's endpoint code optimized normally.
target_compile_options(IDHANOpenAPIDocument PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-lto>)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(IDHANOpenAPIDocument PRIVATE -fconstexpr-steps=10000000)
endif ()
add_dependencies(IDHANOpenAPIDocument IDHANGeneratedRoutes)
target_link_libraries(IDHANServer PRIVATE IDHANOpenAPIDocument)

set(IDHAN_OPENAPI_FILE "${IDHAN_OPENAPI_DIRECTORY}/openapi.json")
# Export from the same compiled object linked into the server. Making the server
# depend on this artifact keeps explicit IDHANServer builds from leaving stale JSON.
add_custom_command(OUTPUT "${IDHAN_OPENAPI_FILE}"
		COMMAND "${CMAKE_COMMAND}" "-DIDHAN_OBJCOPY=${CMAKE_OBJCOPY}"
		"-DIDHAN_BINARY=$<TARGET_OBJECTS:IDHANOpenAPIDocument>"
		"-DIDHAN_OPENAPI_OUTPUT=${IDHAN_OPENAPI_FILE}" -P "${IDHAN_OPENAPI_EXPORTER}"
		DEPENDS IDHANOpenAPIDocument $<TARGET_OBJECTS:IDHANOpenAPIDocument> "${IDHAN_OPENAPI_EXPORTER}"
		COMMENT "Exporting the compiled OpenAPI document"
		VERBATIM)
# Always refresh the tracked snapshot, including after a checkout changes it.
# It is only an output: compilation and the endpoint use the compiled document,
# never the tracked JSON. Do not declare it as a build output: clean must not
# remove repository files.
add_custom_target(IDHANOpenAPI ALL
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/UpdateOpenAPISnapshot.py"
		"${IDHAN_OPENAPI_FILE}" "${CMAKE_SOURCE_DIR}/openapi.json"
		DEPENDS "${CMAKE_CURRENT_LIST_DIR}/UpdateOpenAPISnapshot.py"
		"${IDHAN_OPENAPI_FILE}"
		VERBATIM)
add_dependencies(IDHANServer IDHANOpenAPI)
