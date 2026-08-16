
option(IDHAN_WEB_AUTO_REBUILD "Rebuild the WebUI whenever IDHANServer is built" ON)

find_program(PNPM_EXECUTABLE pnpm)

set(WEB_STAGING_DIR ${CMAKE_BINARY_DIR}/webui-dist)
set(WEB_STATIC_DIR ${CMAKE_BINARY_DIR}/bin/static)

if (NOT PNPM_EXECUTABLE)
	message(WARNING "pnpm not found. Skipping the WebUI build. "
			"The server will serve a placeholder page at '/'. "
			"Install pnpm, or set -DBUILD_IDHAN_WEB=OFF to silence this.")

	file(MAKE_DIRECTORY ${WEB_STATIC_DIR})
	file(WRITE ${WEB_STATIC_DIR}/index.html
			"<!doctype html>\n<html lang=\"en\">\n<head><meta charset=\"UTF-8\"><title>IDHAN</title></head>\n"
			"<body style=\"font:14px/1.5 system-ui,sans-serif;padding:2rem\">\n"
			"<h1>IDHAN</h1>\n"
			"<p>The WebUI was not built: <code>pnpm</code> was not found at configure time.</p>\n"
			"<p>The API is unaffected. See <a href=\"/api\">/api</a> for the documentation.</p>\n"
			"</body>\n</html>\n")

	return()
endif ()

file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/src/*.ts
		${CMAKE_CURRENT_SOURCE_DIR}/src/*.tsx
		${CMAKE_CURRENT_SOURCE_DIR}/src/*.css
		${CMAKE_CURRENT_SOURCE_DIR}/public/*)

set(WEB_CONFIG_FILES
		${CMAKE_CURRENT_SOURCE_DIR}/package.json
		${CMAKE_CURRENT_SOURCE_DIR}/pnpm-lock.yaml
		${CMAKE_CURRENT_SOURCE_DIR}/vite.config.ts
		${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json
		${CMAKE_CURRENT_SOURCE_DIR}/index.html)

add_custom_command(
		OUTPUT ${WEB_STAGING_DIR}/index.html
		COMMAND ${PNPM_EXECUTABLE} install --frozen-lockfile
		COMMAND ${PNPM_EXECUTABLE} exec vite build --outDir ${WEB_STAGING_DIR}
		COMMAND ${CMAKE_COMMAND} -E rm -rf ${WEB_STATIC_DIR}/assets
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${WEB_STAGING_DIR} ${WEB_STATIC_DIR}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		DEPENDS ${WEB_SOURCES} ${WEB_CONFIG_FILES}
		COMMENT "Building the IDHAN WebUI"
		VERBATIM)

add_custom_target(IDHANWeb DEPENDS ${WEB_STAGING_DIR}/index.html)

if (IDHAN_WEB_AUTO_REBUILD AND TARGET IDHANServer)
	add_dependencies(IDHANServer IDHANWeb)
endif ()
