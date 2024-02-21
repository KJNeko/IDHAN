# /cmake_modules/versioninfo.cmake

function(SetVersionInfo)
endfunction()

#	find_package(Git)
#	if (NOT Git_FOUND AND NOT DEFINED BYPASS_GIT_REQUIREMENT)
#		message(FATAL_ERROR
#				"HEY YOU! YEAH YOU! READ ME WITH YOUR EYES. Git was not found!
#                DO **NOT** **EXPECT** **SUPPORT** if your sending us log
#                information without filling these in manually or letting cmake find git.
#                Go read the docs to figure out how to do this. Or fix your git install for cmake")
#	endif ()
#
#	if (DEFINED ATLAS_GIT_BRANCH)
#		message("-- Set git branch string to ${ATLAS_GIT_BRANCH}")
#	else ()
#		#Get the git branch us currently
#		execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE ATLAS_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
#		message("-- Set git branch string to: ${ATLAS_GIT_BRANCH}")
#	endif ()
#
#	if (DEFINED ATLAS_GIT_REVISION)
#		message("-- Set git revision to: ${ATLAS_GIT_REVISION}")
#	else ()
#		execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%H WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE ATLAS_GIT_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
#		message("-- Set git revision to ${ATLAS_GIT_REVISION}")
#	endif ()
#
#	if (DEFINED ATLAS_GIT_TAG)
#		message("-- Git tag set to: ${ATLAS_GIT_TAG}")
#	else ()
#		execute_process(COMMAND ${GIT_EXECUTABLE} ls-remote --tags --sort=-v:refname https://github.com/KJNeko/Atlas.git v*.*.?
#				WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE ATLAS_REMOTE_V_TAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
#		string(REPLACE "\n" ";" ATLAS_REMOTE_V_TAGS_LIST ${ATLAS_REMOTE_V_TAGS})
#		list(LENGTH ATLAS_REMOTE_V_TAGS_LIST ATLAS_REMOTE_TAGS_COUNT)
#		message("-- Tag count: ${ATLAS_REMOTE_TAGS_COUNT}")
#		list(GET ATLAS_REMOTE_V_TAGS_LIST 0 ATLAS_LATEST_TAG)
#		message("-- Latest tag: ${ATLAS_LATEST_TAG}")
#		string(REPLACE "/" ";" ATLAS_TAG_SPLIT ${ATLAS_LATEST_TAG})
#		list(GET ATLAS_TAG_SPLIT 2 ATLAS_PURE_TAG)
#		message("-- Pure tag: ${ATLAS_PURE_TAG}")
#		if (ATLAS_PURE_TAG STREQUAL "")
#			message(FATAL_ERROR "Was unable to pull latest tag from git. Please define it manually via -DATLAS_GIT_TAG.")
#		endif ()
#		set(ATLAS_GIT_TAG ${ATLAS_PURE_TAG})
#		message("-- Git tag set to: ${ATLAS_GIT_TAG}")
#	endif ()
#
#	if (DEFINED ATLAS_GIT_REVISION_BRIEF)
#		message("-- Set git revision to: ${ATLAS_GIT_REVISION_BRIEF}")
#	else ()
#		execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%h WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE ATLAS_GIT_REVISION_BRIEF OUTPUT_STRIP_TRAILING_WHITESPACE)
#		message("-- Set git revision to ${ATLAS_GIT_REVISION_BRIEF}")
#	endif ()
#
#		if (DEFINED ATLAS_GIT_CREATED_TIME)
#		message("-- Set git created time to: ${ATLAS_GIT_CREATED_TIME}")
#	else ()
#		string(TIMESTAMP ATLAS_GIT_CREATED_TIME)
#		message("-- Set git created time to ${ATLAS_GIT_CREATED_TIME}")
#	endif ()
#
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_GIT_BRANCH="${ATLAS_GIT_BRANCH}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_GIT_TAG="${ATLAS_GIT_TAG}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_GIT_REVISION="${ATLAS_GIT_REVISION}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_GIT_REVISION_BRIEF="${ATLAS_GIT_REVISION_BRIEF}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_GIT_CREATED_TIME="${ATLAS_GIT_CREATED_TIME}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_COMPILER_VER="${CMAKE_CXX_COMPILER_VERSION}")
#	target_compile_definitions(${PROJECT_NAME} PRIVATE ATLAS_PLATFORM_ID="${CMAKE_CXX_PLATFORM_ID}")
#endfunction()