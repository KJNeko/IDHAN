set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Widgets)
find_package(ZLIB REQUIRED)
AddFGLExecutable(HydrusImporter ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(HydrusImporter PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network IDHANClient sqlite3 spdlog::spdlog Qt6::Widgets ZLIB::ZLIB)

file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/**.ui)

target_sources(HydrusImporter PRIVATE ${UI_FILES})

target_compile_options(HydrusImporter PRIVATE "-Wno-mismatched-tags")

# TEMPORARY (coroutine frame leak hunt): RowGenerator is the only coroutine in this tool, and this is a
# separate process from the server, so its counters are NOT visible through the server's
# GET /coro/frames -- call idhan::profiling::snapshotCoroFrames() from here to read them. The server
# gets these two settings from the drogon target instead; see dependencies/Finddrogon.cmake.
target_include_directories(HydrusImporter PRIVATE
		${CMAKE_SOURCE_DIR}/dependencies//include)
target_compile_definitions(HydrusImporter PRIVATE $<$<CONFIG:Debug>:IDHAN_TRACK_CORO_FRAMES>)

if (DEFINED IMPORTER_TESTS AND IMPORTER_TESTS EQUAL 1)
	target_compile_definitions(HydrusImporter PUBLIC IMPORTER_TESTS=1)
endif ()

if (DEFINED IMPORTER_PROCESS_PTR_DEFAULT AND IMPORTER_PROCESS_PTR_DEFAULT EQUAL 1)
	target_compile_definitions(HydrusImporter PUBLIC IMPORTER_PROCESS_PTR_DEFAULT=1)
endif ()