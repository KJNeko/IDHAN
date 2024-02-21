#Verify after setting QT_PATH
if (DEFINED QT_PATH)
	message("-- QT_PATH defined as ${QT_PATH}.")
	list(APPEND CMAKE_PREFIX_PATH ${QT_PATH})
else ()
	message("-- QT_PATH not defined.")
endif ()

find_package(Qt6 COMPONENTS Widgets Core Concurrent Network Test Charts REQUIRED)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
