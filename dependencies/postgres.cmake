add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libpqxx)

get_target_property(PQXX_INCLUDE_DIRS pqxx INTERFACE_INCLUDE_DIRECTORIES)
if (PQXX_INCLUDE_DIRS)
	target_include_directories(pqxx SYSTEM INTERFACE ${PQXX_INCLUDE_DIRS})
endif ()