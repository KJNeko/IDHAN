# /cmake_modules/apple.cmake

if (APPLE)
	set(which_program "which")
	set(os_path_separator "/")
	function(PlatformPreSetup)
	endfunction()

	function(PlatformPostSetup)
	endfunction()
endif ()
