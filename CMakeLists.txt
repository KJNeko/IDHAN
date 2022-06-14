cmake_minimum_required(VERSION 3.1)
project(main LANGUAGES CXX)

set(CONAN_SYSTEM_INCLUDES ON)
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

include(FindPkgConfig)
pkg_check_modules(vips REQUIRED IMPORTED_TARGET vips-cpp)

add_executable(main src/main.cpp include/database.cpp include/crypto.cpp include/utility/fileutils.cpp include/services/thumbnailer.cpp include/services/thumbnailer.hpp)
target_include_directories(main PRIVATE ${CONAN_SYSTEM_INCLUDES})
target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/include)
target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/libs)
target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/libs/libFGL/include)
target_include_directories(main PRIVATE ${CMAKE_BINARY_DIR}/libs/tracy)
target_compile_features(main PRIVATE cxx_std_20)
target_compile_options(main PRIVATE -O3 -fmax-errors=3 -Wpadded -fconcepts-diagnostics-depth=4 -fconstexpr-ops-limit=67108864 -Wall -Wextra -Wundef -Wnull-dereference -Wpedantic -pedantic-errors -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override  -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wshadow -Wshadow=local -Wmultiple-inheritance -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wmismatched-tags -Wredundant-tags -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported -Werror=pedantic -Wwrite-strings -Wmultiple-inheritance -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wswitch-enum -Wstrict-overflow=5 -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines -Winvalid-imported-macros -Winvalid-imported-macros -DNDEBUG -Ofast -march=native -fgcse-las -fgcse-sm -fdeclone-ctor-dtor -fdevirtualize-speculatively -fdevirtualize-at-ltrans -ftree-loop-im -fivopts -ftree-loop-ivcanon -fira-hoist-pressure -fsched-pressure -fsched-spec-load -fipa-pta -flto -s -ffat-lto-objects -fno-enforce-eh-specs -fstrict-enums)
target_compile_definitions(main PRIVATE FGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER)
target_link_libraries(main ${CONAN_LIBS} PkgConfig::vips)


add_executable(DBTest src/databaseTests.cpp include/database.cpp include/crypto.cpp include/utility/fileutils.cpp include/services/thumbnailer.cpp include/services/thumbnailer.hpp)
target_include_directories(DBTest PRIVATE ${CONAN_SYSTEM_INCLUDES})
target_include_directories(DBTest PRIVATE ${CMAKE_BINARY_DIR}/include)
target_include_directories(DBTest PRIVATE ${CMAKE_BINARY_DIR}/libs)
target_include_directories(DBTest PRIVATE ${CMAKE_BINARY_DIR}/libs/libFGL/include)
target_include_directories(DBTest PRIVATE ${CMAKE_BINARY_DIR}/libs/tracy)
target_compile_features(DBTest PRIVATE cxx_std_20)
target_compile_options(main PRIVATE -O3 -fmax-errors=3 -Wpadded -fconcepts-diagnostics-depth=4 -fconstexpr-ops-limit=67108864 -Wall -Wextra -Wundef -Wnull-dereference -Wpedantic -pedantic-errors -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override  -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wshadow -Wshadow=local -Wmultiple-inheritance -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wmismatched-tags -Wredundant-tags -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported -Werror=pedantic -Wwrite-strings -Wmultiple-inheritance -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wswitch-enum -Wstrict-overflow=5 -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines -Winvalid-imported-macros -Winvalid-imported-macros -DNDEBUG -Ofast -march=native -fgcse-las -fgcse-sm -fdeclone-ctor-dtor -fdevirtualize-speculatively -fdevirtualize-at-ltrans -ftree-loop-im -fivopts -ftree-loop-ivcanon -fira-hoist-pressure -fsched-pressure -fsched-spec-load -fipa-pta -flto -s -ffat-lto-objects -fno-enforce-eh-specs -fstrict-enums)
target_compile_definitions(DBTest PRIVATE FGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER)
target_link_libraries(DBTest ${CONAN_LIBS} PkgConfig::vips)


set(TRACY_NO_EXIT 1)
set(TRACY_ON_DEMAND 0)
add_subdirectory(libs/tracy)

add_executable(TracyProfile src/databaseTests.cpp include/database.cpp include/crypto.cpp include/utility/fileutils.cpp include/services/thumbnailer.cpp include/services/thumbnailer.hpp)
target_include_directories(TracyProfile PRIVATE ${CONAN_SYSTEM_INCLUDES})
target_include_directories(TracyProfile PRIVATE ${CMAKE_BINARY_DIR}/include)
target_include_directories(TracyProfile PRIVATE ${CMAKE_BINARY_DIR}/libs)
target_include_directories(TracyProfile PRIVATE ${CMAKE_BINARY_DIR}/libs/libFGL/include)
target_include_directories(TracyProfile PRIVATE ${CMAKE_BINARY_DIR}/libs/tracy)
target_compile_features(TracyProfile PRIVATE cxx_std_20)
target_compile_options(TracyProfile PRIVATE -std=c++20 -fmax-errors=3 -Wpadded -fconcepts-diagnostics-depth=4 -fconstexpr-ops-limit=67108864 -Wall -Wextra -Wundef -Wnull-dereference -Wpedantic -pedantic-errors -Weffc++ -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override  -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wfloat-equal -Wshadow -Wshadow=local -Wmultiple-inheritance -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wmismatched-tags -Wredundant-tags -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported -Werror=pedantic -Wwrite-strings -Wmultiple-inheritance -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wswitch-enum -Wstrict-overflow=5 -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines -Winvalid-imported-macros -Winvalid-imported-macros -DNDEBUG -Ofast -march=native -fgcse-las -fgcse-sm -fdeclone-ctor-dtor -fdevirtualize-speculatively -fdevirtualize-at-ltrans -ftree-loop-im -fivopts -ftree-loop-ivcanon -fira-hoist-pressure -fsched-pressure -fsched-spec-load -fipa-pta -flto -s -ffat-lto-objects -fno-enforce-eh-specs -fstrict-enums)
target_compile_definitions(TracyProfile PRIVATE FGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER TRACY_ENABLE TRACY_NO_EXIT)
target_link_libraries(TracyProfile ${CONAN_LIBS} PkgConfig::vips Tracy::TracyClient)


include(CTest)
include(Catch)
catch_discover_tests(DBTest)





