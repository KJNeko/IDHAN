cmake_minimum_required(VERSION 3.28)

# Only remove files previously staged by this operation. Generated API clients
# and independently supplied parser packages can share the destination directory.
file(MAKE_DIRECTORY "${DESTINATION}")
# These generated packages belonged to the removed client-generation pipeline.
file(REMOVE_RECURSE "${DESTINATION}/packages/pawchive-api")
file(REMOVE "${DESTINATION}/packages/zod.js")
set(STAGED_FILES "")

file(GLOB_RECURSE FILES RELATIVE "${SOURCE}" LIST_DIRECTORIES false "${SOURCE}/*")
# Exclude local dependencies and output, e.g. sdk/node_modules/ and dist/gelbooru.js.
list(FILTER FILES EXCLUDE REGEX "(^|/)(node_modules|dist)/")

# Package manifests and TypeScript are authoring input only.
list(FILTER FILES EXCLUDE REGEX "(^|/)package(-lock)?\\.json$")
list(FILTER FILES EXCLUDE REGEX "\\.(ts|mts|cts|tsx)$")

foreach (RELATIVE IN LISTS FILES)
	get_filename_component(PARENT "${DESTINATION}/${RELATIVE}" DIRECTORY)
	file(MAKE_DIRECTORY "${PARENT}")
	file(COPY_FILE "${SOURCE}/${RELATIVE}" "${DESTINATION}/${RELATIVE}" ONLY_IF_DIFFERENT)
	list(APPEND STAGED_FILES "${RELATIVE}")
endforeach ()

file(GLOB_RECURSE JAVASCRIPT RELATIVE "${EMITTED}" LIST_DIRECTORIES false
		"${EMITTED}/*.js" "${EMITTED}/*.mjs" "${EMITTED}/*.js.map" "${EMITTED}/*.mjs.map")
foreach (RELATIVE IN LISTS JAVASCRIPT)
	get_filename_component(PARENT "${DESTINATION}/${RELATIVE}" DIRECTORY)
	file(MAKE_DIRECTORY "${PARENT}")
	file(COPY_FILE "${EMITTED}/${RELATIVE}" "${DESTINATION}/${RELATIVE}" ONLY_IF_DIFFERENT)
	list(APPEND STAGED_FILES "${RELATIVE}")
endforeach ()

if (EXISTS "${MANIFEST}")
	file(STRINGS "${MANIFEST}" PREVIOUS_FILES)

	foreach (PREVIOUS IN LISTS PREVIOUS_FILES)
		if (NOT PREVIOUS IN_LIST STAGED_FILES)
			file(REMOVE "${DESTINATION}/${PREVIOUS}")
		endif ()
	endforeach ()
endif ()

list(JOIN STAGED_FILES "\n" FILE_LIST)
file(WRITE "${MANIFEST}" "${FILE_LIST}\n")

