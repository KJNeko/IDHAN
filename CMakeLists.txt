cmake_minimum_required(VERSION 3.15)
project(IDHAN LANGUAGES CXX)

if (WIN32)
    list(APPEND CMAKE_PREFIX_PATH "D:\\Qt\\6.3.1\\mingw_64")
endif ()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/interface/ui)

find_package(Qt6 COMPONENTS Widgets Core Concurrent REQUIRED)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/spdlog)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/tracy)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/submodules/libpqxx)

set(CMAKE_CXX_FLAGS_DEBUG "-std=c++20 -Og -g -rdynamic -fmax-errors=3 -fconcepts-diagnostics-depth=4 -fconstexpr-ops-limit=67108864 -Wall -Wextra -Wundef -Wnull-dereference -Wpedantic -pedantic-errors -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override  -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wshadow -Wshadow=local -Wmultiple-inheritance -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported -Werror=pedantic -Wwrite-strings -Wmultiple-inheritance -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wswitch-enum -Wstrict-overflow=5 -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines -Winvalid-imported-macros -Winvalid-imported-macros -fstrict-aliasing -fno-omit-frame-pointer -fstack-check -ftrapv -fwrapv -fverbose-asm -femit-class-debug-always")
set(CMAKE_CXX_FLAGS_RELEASE "-std=c++20 -O3 -DNDEBUG -s")
#System specific release flags
set(CMAKE_CXX_FLAGS_SYSTEM "-std=c++20 -DNDEBUG -O3 -march=native -fmax-errors=3 -fgcse-las -fgcse-sm -fdeclone-ctor-dtor -fdevirtualize-speculatively -fdevirtualize-at-ltrans -ftree-loop-im -fivopts -ftree-loop-ivcanon -fira-hoist-pressure -fsched-pressure -fsched-spec-load -fipa-pta -flto=auto -s -ffat-lto-objects -fno-enforce-eh-specs -fstrict-enums")

message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

#If we are compiling in debug then enable tracy
if (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(TRACY_ENABLE 1)
    set(TRACY_NO_EXIT 1)
    set(TRACY_ON_DEMAND 0)
    message("Tracy enabled")
endif ()

file(GLOB_RECURSE SOURCES
        "${CMAKE_SOURCE_DIR}/include/*.cpp"
        "${CMAKE_SOURCE_DIR}/interface/*.ui"
        "${CMAKE_SOURCE_DIR}/interface/*.cpp"
        "${CMAKE_SOURCE_DIR}/Resources/*.qrc"
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        )

include(tests/database/CMakeLists.txt)

add_executable(IDHAN ${SOURCES})

string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPER)
message("${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE_UPPER}}")

target_include_directories(IDHAN PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(IDHAN PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(IDHAN PUBLIC ${CMAKE_SOURCE_DIR}/interface)

target_include_directories(IDHAN PUBLIC ${CMAKE_SOURCE_DIR}/submodules/libpqxx/include)
target_include_directories(IDHAN PUBLIC ${CMAKE_SOURCE_DIR}/submodules/spdlog/include)
target_include_directories(IDHAN PUBLIC ${CMAKE_SOURCE_DIR}/submodules/libFGL/include)

target_compile_features(IDHAN PRIVATE cxx_std_20)
target_compile_definitions(IDHAN PRIVATE FGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER TRACY_ENABLE)

target_link_libraries(IDHAN PUBLIC Qt6::Core Qt6::Widgets Qt6::Concurrent spdlog pqxx TracyClient)

#Unit tests



