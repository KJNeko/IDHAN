# Compile authored fixtures before the C++ tests; runtime tests only see JavaScript.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/packages")
file(COPY "${SOURCE}/IDHANDownloader/premade/" DESTINATION "${WORK}/premade"
		PATTERN "node_modules" EXCLUDE)
file(COPY_FILE "${SOURCE}/tests/downloader/contracts/parser-contracts.ts" "${WORK}/packages/contracts.ts")
file(COPY_FILE "${SOURCE}/tests/downloader/fixtures/options.ts" "${WORK}/options.ts")
file(COPY_FILE "${SOURCE}/IDHANDownloader/tsconfig.json" "${WORK}/tsconfig.json")
file(WRITE "${WORK}/package.json" "{\"type\":\"module\"}\n")
file(READ "${WORK}/tsconfig.json" CONFIG_TEXT)
string(JSON CONFIG_TEXT SET "${CONFIG_TEXT}" include "[\"**/*.ts\"]")
string(JSON CONFIG_TEXT SET "${CONFIG_TEXT}" compilerOptions noEmit false)
string(JSON CONFIG_TEXT SET "${CONFIG_TEXT}" compilerOptions noEmitOnError true)
string(JSON CONFIG_TEXT SET "${CONFIG_TEXT}" compilerOptions rootDir "\".\"")
file(WRITE "${WORK}/tsconfig.json" "${CONFIG_TEXT}")
file(COPY "${SOURCE}/IDHANDownloader/premade/packages/sdk" DESTINATION "${WORK}/packages")
file(REMOVE_RECURSE "${WORK}/premade/packages")
foreach (PARSER e621 gelbooru hyshare pixiv pawchive discord twitter mega)
	file(READ "${WORK}/premade/${PARSER}/index.ts" TEXT)
	string(REPLACE "../packages/sdk/" "../../packages/sdk/" TEXT "${TEXT}")
	file(WRITE "${WORK}/premade/${PARSER}/index.ts" "${TEXT}")
endforeach ()
set(CONFIG "${WORK}/tsconfig.json")
execute_process(
		COMMAND "${NODE}" "${SOURCE}/IDHANDownloader/scripts/build-parsers.mjs"
		--source "${WORK}" --config "${CONFIG}" --outdir "${OUTPUT_DIR}"
		--npm-dir "${NPM_DIR}" --all-modules
		RESULT_VARIABLE STATUS)
if (NOT STATUS EQUAL 0)
	message(FATAL_ERROR "Parser fixture bundling failed (${STATUS})")
endif ()
