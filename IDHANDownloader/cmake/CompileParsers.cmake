function(idhan_compile_parsers TARGET_NAME SOURCE_DIR CONFIG OUTPUT_DIR)
	file(GLOB_RECURSE PARSER_TYPESCRIPT CONFIGURE_DEPENDS "${SOURCE_DIR}/*.ts" "${SOURCE_DIR}/*.mts")
	# Match sources under generated output or installed dependency directories.
	list(FILTER PARSER_TYPESCRIPT EXCLUDE REGEX "/(dist|node_modules)/")
	file(GLOB_RECURSE PARSER_JAVASCRIPT CONFIGURE_DEPENDS "${SOURCE_DIR}/packages/*.js" "${SOURCE_DIR}/packages/*.mjs" "${SOURCE_DIR}/packages/*.mts")
	file(GLOB PARSER_MANIFESTS CONFIGURE_DEPENDS "${SOURCE_DIR}/*/package.json")
	set(PARSER_BUNDLES "${OUTPUT_DIR}/.idhan-bundles.json")
	foreach (SOURCE IN LISTS PARSER_TYPESCRIPT)
		file(RELATIVE_PATH RELATIVE "${SOURCE_DIR}" "${SOURCE}")
		# Shared modules are embedded in their entry bundles. Discovery is its own entry.
		# Match declaration modules such as packages/sdk/parser.d.ts.
		if (RELATIVE MATCHES "\\.d\\.(ts|mts)$")
			continue()
		endif ()
		# Match modules beneath a packages directory, such as packages/sdk/runtime.ts.
		if (RELATIVE MATCHES "(^|/)packages/" AND NOT RELATIVE STREQUAL "packages/sdk/discovery.ts")
			continue()
		endif ()
		get_filename_component(PACKAGE_DIR "${SOURCE}" DIRECTORY)
		if (EXISTS "${PACKAGE_DIR}/package.json")
			file(READ "${PACKAGE_DIR}/package.json" PACKAGE_JSON)
			string(JSON ENTRY GET "${PACKAGE_JSON}" idhanParser)
			get_filename_component(ENTRY_NAME "${SOURCE}" NAME)
			if (NOT ENTRY_NAME STREQUAL ENTRY)
				continue()
			endif ()
			file(RELATIVE_PATH PACKAGE_RELATIVE "${SOURCE_DIR}" "${PACKAGE_DIR}")
			list(APPEND PARSER_BUNDLES "${OUTPUT_DIR}/${PACKAGE_RELATIVE}.js")
			continue()
		endif ()
		# Replace the source extension on a loose entry with .js.
		string(REGEX REPLACE "\\.(ts|mts)$" ".js" BUNDLE "${RELATIVE}")
		list(APPEND PARSER_BUNDLES "${OUTPUT_DIR}/${BUNDLE}")
	endforeach ()
	set(PARSER_MAPS "")
	foreach (BUNDLE IN LISTS PARSER_BUNDLES)
		if (BUNDLE MATCHES "\\.js$")
			list(APPEND PARSER_MAPS "${BUNDLE}.map")
		endif ()
	endforeach ()
	list(APPEND PARSER_BUNDLES ${PARSER_MAPS})
	set(INPUT_LIST "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}-inputs.txt")
	file(GENERATE OUTPUT "${INPUT_LIST}" CONTENT "${PARSER_TYPESCRIPT};${PARSER_JAVASCRIPT};${PARSER_MANIFESTS}\n")
	set(STAMP "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.stamp")
	get_filename_component(CONFIG_DIR "${CONFIG}" DIRECTORY)
	add_custom_command(
			OUTPUT "${STAMP}" ${PARSER_BUNDLES}
			COMMAND ${NODE_EXECUTABLE} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/build-parsers.mjs"
			--source "${SOURCE_DIR}" --config "${CONFIG}" --outdir "${OUTPUT_DIR}"
			--npm-dir "${IDHAN_DOWNLOADER_NPM_DIR}"
			COMMAND ${CMAKE_COMMAND} -E touch "${STAMP}"
			DEPENDS "${IDHAN_DOWNLOADER_NPM_STAMP}" "${INPUT_LIST}" "${CONFIG}"
			"${CONFIG_DIR}/tsconfig.json" ${PARSER_TYPESCRIPT} ${PARSER_JAVASCRIPT} ${PARSER_MANIFESTS}
			"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/build-parsers.mjs"
			COMMENT "Compiling ${TARGET_NAME} into JavaScript bundles"
			VERBATIM)
	add_custom_target(${TARGET_NAME} DEPENDS "${STAMP}" ${PARSER_BUNDLES})
endfunction()
