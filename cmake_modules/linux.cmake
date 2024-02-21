# /cmake_modules/linux.cmake

if (UNIX AND (NOT APPLE))
	set(which_program "which")
	set(os_path_separator "/")
	function(PlatformPreSetup)
	endfunction()

	function(PlatformPostSetup)
	endfunction()
endif ()    # if(UNIX AND (NOT APPLE))
