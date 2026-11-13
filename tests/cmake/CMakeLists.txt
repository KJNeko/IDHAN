foreach (OPTIONS_CASE IN ITEMS tests docs http trace)
	add_test(NAME IDHANCMakeOptions_${OPTIONS_CASE}
			COMMAND "${CMAKE_COMMAND}" "-DIDHAN_OPTIONS_CASE=${OPTIONS_CASE}"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Options.cmake")
endforeach ()
