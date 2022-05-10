cmake_minimum_required(VERSION 3.14)

if (WIN32)
    project(IDHAN LANGUAGES CXX)
elseif (UNIX)
    project(IDHAN)
endif ()

set(CMAKE_GENERATOR "Unix Makefiles")
SET(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CONFIGURATION_TYPES "Release;Dev" CACHE STRING "" FORCE)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
# -fsanitize=address
# -fmodules-ts

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -Og -DDEBUG -lpqxx -lssl -lcrypto -lpq -Werror=cpp -Wall -Wextra -pthread -pedantic -fmax-errors=3 -Wundef -Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wshadow -Wswitch -Wswitch-enum -Wstrict-overflow=5 -Wconversion -Wfloat-equal -Wdouble-promotion -Wwrite-strings -Wpointer-arith -Wcast-qual -Wformat=2 -Wformat-signedness -Wformat-y2k -Wformat-overflow=2 -Wold-style-cast -Wnon-virtual-dtor -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override -Wsized-deallocation -Wextra-semi -Wmultiple-inheritance -Wzero-as-null-pointer-constant -Werror") #-fuse-linker-plugin -flto -fno-fat-lto-objects")
#-fanalyzer

set(GCC_WARNING_MINIMUM "-Wall -Wextra -Wundef -Wnull-dereference")
set(GCC_WARNING_RELAXED "-Wpedantic -pedantic-errors -Weffc++ -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override  -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wshadow -Wshadow=local -Wmultiple-inheritance -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wmismatched-tags -Wredundant-tags -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported")
set(GCC_WARNING_STRICT "-Werror=pedantic -Wwrite-strings -Wmultiple-inheritance -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wswitch-enum -Wstrict-overflow=5 -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines")

set(GCC_USER_CONFIG "-lz -std=c++23 -ldl -lpqxx -lpq -I./include/libFGL/include -DFGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER")

set(GCC_CODEGEN "-Og -march=native -fgcse-las -fgcse-sm -fdeclone-ctor-dtor -fdevirtualize-speculatively -fdevirtualize-at-ltrans -ftree-loop-im -fivopts -ftree-loop-ivcanon -fira-hoist-pressure -fsched-pressure -fsched-spec-load -fipa-pta -flto -ffat-lto-objects -fno-enforce-eh-specs -fstrict-enums")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MAKE_PROGRAM make)

set(CMAKE_CXX_FLAGS "${GCC_USER_CONFIG} ${GCC_WARNING_MINIMUM} ${GCC_WARNING_RELAXED} ${GCC_WARNING_STRICT} ${GCC_CODEGEN}")


set(CMAKE_INCLUDE_CURRENT_DIR ON)
#inclusion of project files#
set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")

include_directories(${INCLUDE_DIR})
include_directories(${SOURCE_DIR})


file(GLOB_RECURSE SOURCES
        "${SOURCE_DIR}/*.cpp"
        "${SOURCE_DIR}/*.hlsl"
        "${SOURCE_DIR}/*.comp"
        "${INCLUDE_DIR}/*.hpp" mimeProcessing.cpp
        hashProcessing.cpp
        )

##SETUP EXE##

set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_AUTOUIC_SEARCH_PATHS})

add_executable(IDHAN ${SOURCES})

target_include_directories(IDHAN PRIVATE ${INCLUDE_DIR})
target_include_directories(IDHAN PRIVATE ${SOURCE_DIR})
