# Qt-free core of the PTR pipeline: update-file parsing plus every flatten primitive.
# Deliberately separate from HydrusImporter so IDHANCoreTests can link it without pulling
# in Qt, drogon, or a live PostgreSQL. Must stay outside HydrusImporter/src, which is
# glob-recursed by AddFGLExecutable.

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

AddFGLLibrary(PTRCore STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

# IDHAN is PUBLIC because it transitively supplies Jsoncpp_lib, and PTRFileParser.hpp
# includes <json/json.h>.
target_link_libraries(PTRCore PUBLIC IDHAN)
target_link_libraries(PTRCore PRIVATE ZLIB::ZLIB spdlog::spdlog Threads::Threads)
