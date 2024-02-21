# /cmake_modules/utils.cmake

set(os_path_separator "")
FUNCTION(GET_PARENT_PATH absolute_path path_separator return_var)
	message(DEBUG "absolute_path: ${absolute_path}")
	string(REPLACE ${path_separator} ";" absolute_path_list ${absolute_path})  # Converts Compiler path to ';' seperated list
	message(DEBUG "absolute_path_list: ${absolute_path_list}")
	LIST(POP_BACK absolute_path_list)
	message(DEBUG "absolute_path_list: ${absolute_path_list}")
	LIST(JOIN absolute_path_list "/" parent_path)  # Converts ';' seperated list to path
	message(DEBUG "parent_path: ${parent_path}")
	SET(${return_var} ${parent_path} PARENT_SCOPE)
	RETURN(${return_var})
ENDFUNCTION()

set(which_program "")   # To be filled in by win32, linux, or apple modules.
FUNCTION(WHICH program2find)
	EXECUTE_PROCESS(
			COMMAND ${which_program} ${program2find}
			RESULT_VARIABLE exit_code
			OUTPUT_VARIABLE return_value
	)

ENDFUNCTION()