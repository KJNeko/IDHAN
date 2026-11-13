set(IDHAN_ROUTE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(IDHAN_ROUTE_TEST_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/route-discovery")
file(GLOB_RECURSE IDHAN_ROUTE_FIXTURE_INPUTS CONFIGURE_DEPENDS "${IDHAN_ROUTE_FIXTURES}/*.hpp")
add_custom_command(OUTPUT "${IDHAN_ROUTE_TEST_GENERATED}/gen_routes.hpp"
		COMMAND "${CMAKE_COMMAND}" "-DIDHAN_ROUTE_ROOT=${IDHAN_ROUTE_FIXTURES}"
		"-DIDHAN_ROUTE_OUTPUT=${IDHAN_ROUTE_TEST_GENERATED}/gen_routes.hpp"
		-P "${CMAKE_SOURCE_DIR}/IDHANServer/cmake/GenerateRoutes.cmake"
		DEPENDS ${IDHAN_ROUTE_FIXTURE_INPUTS} "${CMAKE_SOURCE_DIR}/IDHANServer/cmake/GenerateRoutes.cmake"
		VERBATIM)
add_custom_target(IDHANTestGeneratedRoutes DEPENDS "${IDHAN_ROUTE_TEST_GENERATED}/gen_routes.hpp")
AddFGLExecutable(IDHANRouteDiscoveryTests ${CMAKE_CURRENT_SOURCE_DIR}/runtime)
target_include_directories(IDHANRouteDiscoveryTests PRIVATE "${IDHAN_ROUTE_TEST_GENERATED}" "${IDHAN_ROUTE_FIXTURES}")
target_link_libraries(IDHANRouteDiscoveryTests PRIVATE Catch2::Catch2WithMain IDHANHttp)
add_dependencies(IDHANRouteDiscoveryTests IDHANTestGeneratedRoutes)
catch_discover_tests(IDHANRouteDiscoveryTests)
add_test(NAME IDHANRouteHeaderGenerator
		COMMAND "${CMAKE_COMMAND}" "-DIDHAN_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/route-generator-tests"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/test_generator.cmake")
