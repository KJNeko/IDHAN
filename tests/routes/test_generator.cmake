cmake_minimum_required(VERSION 3.28)

if (NOT DEFINED IDHAN_TEST_DIRECTORY)
	message(FATAL_ERROR "Set IDHAN_TEST_DIRECTORY to a scratch directory")
endif ()
set(GENERATOR "${CMAKE_CURRENT_LIST_DIR}/../../IDHANServer/cmake/GenerateRoutes.cmake")
include("${GENERATOR}")

function(check_routes TEXT EXPECTED)
	idhan_has_route_annotations("${TEXT}" ACTUAL)
	if (NOT "${ACTUAL}" STREQUAL "${EXPECTED}")
		message(FATAL_ERROR "Expected ${EXPECTED}, got ${ACTUAL}: ${TEXT}")
	endif ()
endfunction()

check_routes([==[// [[= http::Get{"/fake"}]]]==] FALSE)
check_routes([==[/* [[= http::Get{"/fake"}]] */]==] FALSE)
check_routes([==[auto text = R"tag([[= http::Get{"/fake"}]])tag";]==] FALSE)
check_routes([==[auto text = "[[= http::Get{\"/fake\"}]]";]==] FALSE)
check_routes([==[[[= idhan::http::Post{"/real"}]]]==] TRUE)
check_routes([==[[[= Public{}, = Get{"/real"}]]]==] TRUE)
check_routes([==[auto c = L'a'; auto n = 0xAB'CD; [[= http::Get{"/real"}]]]==] TRUE)
check_routes([==[
[
 [
 = http::Get{"/multiline"}
 ]
]
]==] TRUE)
check_routes([==[// [[= http::Get{"/fake"}]]
[[= http::Post{"/real"}]]]==] TRUE)

file(REMOVE_RECURSE "${IDHAN_TEST_DIRECTORY}")
set(ROOT "${IDHAN_TEST_DIRECTORY}/src")
set(OUTPUT "${IDHAN_TEST_DIRECTORY}/gen_routes.hpp")
file(MAKE_DIRECTORY "${ROOT}")
file(WRITE "${ROOT}/first.hpp" [==[[[= http::Get{"/first"}]]]==])
idhan_generate_routes("${ROOT}" "${OUTPUT}")
file(READ "${OUTPUT}" INITIAL)
string(FIND "${INITIAL}" "#include \"first.hpp\"" FOUND)
if (FOUND EQUAL -1)
	message(FATAL_ERROR "The first route header was not included")
endif ()

file(WRITE "${ROOT}/second.hpp" [==[[[= http::Post{"/second"}]]]==])
idhan_generate_routes("${ROOT}" "${OUTPUT}")
file(READ "${OUTPUT}" BOTH)
string(FIND "${BOTH}" "#include \"second.hpp\"" FOUND)
if (FOUND EQUAL -1)
	message(FATAL_ERROR "A newly added route header was not included")
endif ()
file(TIMESTAMP "${OUTPUT}" BEFORE "%s.%f")
idhan_generate_routes("${ROOT}" "${OUTPUT}")
file(TIMESTAMP "${OUTPUT}" AFTER "%s.%f")
if (NOT BEFORE STREQUAL AFTER)
	message(FATAL_ERROR "An unchanged generated header was rewritten")
endif ()

file(REMOVE "${ROOT}/first.hpp")
idhan_generate_routes("${ROOT}" "${OUTPUT}")
file(READ "${OUTPUT}" LAST)
string(FIND "${LAST}" "#include \"first.hpp\"" FOUND)
if (NOT FOUND EQUAL -1)
	message(FATAL_ERROR "A deleted route header is still included")
endif ()

file(WRITE "${ROOT}/route.cpp" [==[[[= http::Get{"/bad"}]]]==])
execute_process(COMMAND "${CMAKE_COMMAND}" "-DIDHAN_ROUTE_ROOT=${ROOT}" "-DIDHAN_ROUTE_OUTPUT=${OUTPUT}"
		-P "${GENERATOR}" RESULT_VARIABLE STATUS OUTPUT_VARIABLE LOG ERROR_VARIABLE ERROR)
if (STATUS EQUAL 0 OR NOT ERROR MATCHES "declared in a header")
	message(FATAL_ERROR "The source-only route was not rejected: ${LOG}${ERROR}")
endif ()
file(REMOVE_RECURSE "${IDHAN_TEST_DIRECTORY}")
message(STATUS "CMake route generator checks passed")
