

if (NOT TARGET Trantor::Trantor)

	set(USE_SPDLOG ON)

	# Trantor apparently uses a really shitty install thing from cmake.
	# There is no way to disable it, So we do this instead. What. The. Fuck?
	macro(install)
	endmacro()

	add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/trantor ${CMAKE_CURRENT_BINARY_DIR}/trantor)
	set(Trantor_FOUND TRUE)

	add_library(Trantor::Trantor ALIAS trantor)

endif ()