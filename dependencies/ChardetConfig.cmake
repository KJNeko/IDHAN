# set default path if not provided
if (NOT CHARDET_ROOT)
	set(CHARDET_ROOT "${IDHAN_DEPENDENCIES}/chardet")
endif ()

file(GLOB_RECURSE CHARDET_SOURCES ${CHARDET_ROOT}/src/**.cpp)

add_library(chardet ${CHARDET_SOURCES})
target_include_directories(chardet PUBLIC ${CHARDET_ROOT}/src)
target_include_directories(chardet PUBLIC ${CHARDET_ROOT}/include)
