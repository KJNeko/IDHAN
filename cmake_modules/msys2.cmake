

SET(COPY_MSYS2_BINARIES OFF CACHE BOOL "Whether to copy the DLLs from the corresponding MSYS2 prefix")
IF (${COPY_MSYS2_BINARIES})
	GET_PARENT_PATH(${CMAKE_C_COMPILER} "/" compiler_folder)
	MESSAGE(DEBUG "compiler_folder: ${compiler_folder}")
	MESSAGE(DEBUG "BINARY FOLDER: ${BINARY_FOLDER}")
	SET(msys2_prefix_bin ${compiler_folder})
	FOREACH (
			DLL
			libsqlite3-0.dll
			libgcc_s_seh-1.dll
			libgcc_s_dw2-1.dll
			libstdc++-6.dll
			libwinpthread-1.dll
	)
		SET(file2copy "${msys2_prefix_bin}/${DLL}")
		IF (EXISTS ${file2copy})
			MESSAGE(DEBUG "COPYING: ${file2copy}")
			CONFIGURE_FILE(${file2copy} "${BINARY_FOLDER}/${DLL}" COPYONLY)
		endif () # EXISTS ${file2copy}
	ENDFOREACH ()
ENDIF ()
