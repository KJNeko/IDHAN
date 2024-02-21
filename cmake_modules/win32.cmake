# /cmake_modules/win32.cmake

if (WIN32)
	set(which_program "where")
	set(os_path_separator "\\")

	set(
			NEEDED_QT_FOLDERS
			"${CMAKE_BINARY_DIR}/bin/data"
			"${CMAKE_BINARY_DIR}/bin/iconengines"
			"${CMAKE_BINARY_DIR}/bin/imageformats"
			"${CMAKE_BINARY_DIR}/bin/networkinformation"
			"${CMAKE_BINARY_DIR}/bin/platforms"
			"${CMAKE_BINARY_DIR}/bin/styles"
			"${CMAKE_BINARY_DIR}/bin/tls"
	)
	function(PlatformPreSetup)
	endfunction()   # PlatformPreSetup

	function(PlatformPostSetup)
	endfunction()   # PlatformPostSetup

endif ()    # if (WIN32)
