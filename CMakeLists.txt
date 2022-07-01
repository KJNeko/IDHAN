cmake_minimum_required(VERSION 3.15)
project(main LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/interface/ui)

#set(CMAKE_THREAD_LIBS_INIT "-lpthread")
#set(CMAKE_HAVE_THREADS_LIBRARY 1)
#set(CMAKE_USE_WIN32_THREADS_INIT 0)
#set(CMAKE_USE_PTHREADS_INIT 1)
#set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CONAN_SYSTEM_INCLUDES ON)
include(${CMAKE_SOURCE_DIR}/conan/conan_paths.cmake)
#include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
#conan_basic_setup()

find_package("spdlog")

find_package("libpqxx")

find_package("libFGL")

find_package(Qt6 COMPONENTS Widgets Core Concurrent REQUIRED)

set(TRACY_ENABLE 1)
set(TRACY_NO_EXIT 1)
set(TRACY_ON_DEMAND 0)
add_subdirectory(libs/tracy)

file(GLOB_RECURSE SOURCES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/include/*.cpp"
        "${CMAKE_SOURCE_DIR}/include/services/import/*.cpp"
        "${CMAKE_SOURCE_DIR}/interface/src/*.cpp"
        )

set(FGL_COMPILER_FLAGS -std=c++20 -fmax-errors=3 -fconcepts-diagnostics-depth=4 -fconstexpr-ops-limit=67108864 -Wall -Wextra -Wundef -Wnull-dereference -Wpedantic -pedantic-errors -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wshadow -Wshadow=local -Wmultiple-inheritance -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported -Werror=pedantic -Wwrite-strings -Wmultiple-inheritance -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wswitch-enum -Wstrict-overflow=5 -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines -Winvalid-imported-macros -Winvalid-imported-macros -Og -g -fstrict-aliasing -fno-omit-frame-pointer -fstack-check -ftrapv -fwrapv -fverbose-asm -femit-class-debug-always)

add_executable(main ${SOURCES})

target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/libs)
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/libs/tracy)
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/interface/src)
target_include_directories(main PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(main PRIVATE cxx_std_20)
target_compile_options(main PRIVATE ${FGL_COMPILER_FLAGS})
target_compile_definitions(main PRIVATE FGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER)

target_link_libraries(main Qt6::Core Qt6::Widgets Qt6::Concurrent spdlog::spdlog libpqxx::pqxx Tracy::TracyClient libFGL::libFGL)
