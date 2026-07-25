
message("Processing migrations dir ${MIGRATION_DIR}")

# Process each migration file
file(GLOB_RECURSE MIGRATIONS "${MIGRATION_DIR}/*.sql")

file(READ ${MIGRATION_DIR}/migration-template.cpp.unused TEMPLATE_CONTENT)
file(READ ${MIGRATION_DIR}/check-template.cpp.unused CHECK_TEMPLATE_CONTENT)

string(CONFIGURE "${CHECK_TEMPLATE_CONTENT}" CHECK_TEMPLATE_CONTENT)


# Migrations now live in per-object subdirectories (e.g. table_file_info/013-create.sql).
# Ordering is determined solely by the numeric prefix of the basename, GLOBALLY across all
# subdirectories - the directory a file lives in must never affect run order. So we build a
# sort key from the (zero-padded) number rather than sorting on the full path.
set(KEYED_MIGRATIONS "")
foreach (MIGRATION ${MIGRATIONS})
	get_filename_component(FILENAME ${MIGRATION} NAME_WLE)
	if (NOT FILENAME MATCHES "^[0-9]+")
		message(FATAL_ERROR "Migration '${MIGRATION}' has no numeric prefix")
	endif ()
	string(REGEX MATCH "^[0-9]+" MIGRATION_ID ${FILENAME})
	# zero-pad to 6 digits so a plain STRING sort is numerically correct
	string(LENGTH "${MIGRATION_ID}" _len)
	math(EXPR _pad "6 - ${_len}")
	set(_zeros "")
	if (_pad GREATER 0)
		foreach (_i RANGE 1 ${_pad})
			set(_zeros "0${_zeros}")
		endforeach ()
	endif ()
	list(APPEND KEYED_MIGRATIONS "${_zeros}${MIGRATION_ID}::${MIGRATION}")
endforeach ()

list(SORT KEYED_MIGRATIONS COMPARE STRING)

# Fail loudly if two migrations share a global number - they would run in an undefined order.
set(_seen_numbers "")
foreach (ENTRY ${KEYED_MIGRATIONS})
	string(REGEX REPLACE "::.*$" "" _num "${ENTRY}")
	# Use list(FIND) rather than the IN_LIST if() operator: this script runs in `cmake -P` mode
	# where policy CMP0057 is unset, and IN_LIST is only recognised when that policy is NEW.
	list(FIND _seen_numbers "${_num}" _dup_idx)
	if (NOT _dup_idx EQUAL -1)
		message(FATAL_ERROR "Duplicate migration number ${_num}")
	endif ()
	list(APPEND _seen_numbers "${_num}")
endforeach ()

foreach (ENTRY ${KEYED_MIGRATIONS})
	string(REGEX REPLACE "^[0-9]+::" "" MIGRATION "${ENTRY}")

	get_filename_component(FILENAME ${MIGRATION} NAME_WLE)
	string(REGEX MATCH "^[0-9]+" MIGRATION_ID ${FILENAME})
	# Filenames are zero-padded (013, 096) for readable ordering, but that number is emitted
	# verbatim as a C++ integer literal - and a leading zero makes it OCTAL (013 -> 11, 096 -> error).
	# Strip leading zeros so the literal is decimal.
	string(REGEX REPLACE "^0+" "" MIGRATION_ID "${MIGRATION_ID}")
	if (MIGRATION_ID STREQUAL "")
		set(MIGRATION_ID "0")
	endif ()

	# The object this migration belongs to is the parent directory name, e.g. "table_file_info".
	get_filename_component(MIGRATION_DIRPATH ${MIGRATION} DIRECTORY)
	get_filename_component(MIGRATION_TABLE ${MIGRATION_DIRPATH} NAME)

	# The operation is the descriptive part of the filename after the "NNN-" prefix, e.g.
	# "013-create" -> "create", "051-alter" -> "alter". Stored as the object's latest operation.
	string(REGEX REPLACE "^[0-9]+-?" "" MIGRATION_OPERATION "${FILENAME}")
	if (MIGRATION_OPERATION STREQUAL "")
		set(MIGRATION_OPERATION "unknown")
	endif ()

	string(REPLACE "NEXT_MIGRATION" "${CHECK_TEMPLATE_CONTENT}" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")

	file(READ ${MIGRATION} FILE_CONTENT)

	# Compact the query before embedding it: strip SQL line comments (-- to end of line), then
	# collapse every run of whitespace (newlines, tabs, spaces) into a single space. SQL is
	# whitespace-insensitive, so this preserves behaviour while removing the newlines and
	# indentation that would otherwise bloat the generated literal. Comments MUST be stripped
	# first: once the lines are joined, a surviving `--` would comment out the rest of the
	# statement. ('.' matches newlines in CMake regex, hence the explicit [^\n] / [ \t\r\n]
	# classes. No migration uses multi-line or multi-space string literals, so collapsing
	# whitespace never alters literal data.)
	string(REGEX REPLACE "--[^\n]*" "" FILE_CONTENT "${FILE_CONTENT}")
	string(REGEX REPLACE "[ \t\r\n]+" " " FILE_CONTENT "${FILE_CONTENT}")
	string(STRIP "${FILE_CONTENT}" FILE_CONTENT)

	# The #embed branch (FGL_HAS_EMBED) pulls in raw file bytes, so point it at a compacted copy
	# rather than the original source - keeping both branches byte-identical.
	get_filename_component(OUT_DIR ${OUT} DIRECTORY)
	set(COMPACTED_PATH "${OUT_DIR}/migrations_compacted/${MIGRATION_ID}.sql")
	file(WRITE "${COMPACTED_PATH}" "${FILE_CONTENT}")

	string(REPLACE "MIGRATION_TABLE" "${MIGRATION_TABLE}" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")
	string(REPLACE "MIGRATION_OPERATION" "${MIGRATION_OPERATION}" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")
	string(REPLACE "MIGRATION_TARGET_ID" "${MIGRATION_ID}" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")
	string(REPLACE "MIGRATION_QUERY" "${FILE_CONTENT}" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")
	string(REPLACE "MIGRATION_PATH" "${COMPACTED_PATH}" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")
endforeach ()

string(REPLACE "NEXT_MIGRATION" "\treturn migration_id;" TEMPLATE_CONTENT "${TEMPLATE_CONTENT}")

message("--Write out to ${OUT}")
file(WRITE ${OUT} "${TEMPLATE_CONTENT}")
