function(CompilerPreSetup)
	if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
		#These two flags added with older gcc versions and Qt causes a compiler segfault -Wmismatched-tags -Wredundant-tags
		# STL has some warnings that prevent -Werror level compilation "-Wnull-dereference"
		set(FGL_WARNINGS "-Wall -Wundef -Wextra -Wpessimizing-move -Wpedantic -Weffc++ -pedantic-errors -Wnoexcept -Wuninitialized -Wunused -Wunused-parameter -Winit-self -Wconversion -Wuseless-cast -Wextra-semi -Wsuggest-final-types -Wsuggest-final-methods -Wsuggest-override -Wformat-signedness -Wno-format-zero-length -Wmissing-include-dirs -Wshift-overflow=2 -Walloc-zero -Walloca -Wsign-promo -Wconversion -Wduplicated-branches -Wduplicated-cond -Wshadow -Wshadow=local -Wvirtual-inheritance -Wno-virtual-move-assign -Wunsafe-loop-optimizations -Wnormalized -Wpacked -Wredundant-decls -Wctor-dtor-privacy -Wdeprecated-copy-dtor -Wstrict-null-sentinel -Wold-style-cast -Woverloaded-virtual -Wzero-as-null-pointer-constant -Wconditionally-supported -Wwrite-strings -Wunused-const-variable=2 -Wdouble-promotion -Wpointer-arith -Wcast-align=strict -Wcast-qual -Wconversion -Wsign-conversion -Wimplicit-fallthrough=1 -Wmisleading-indentation -Wdangling-else -Wdate-time -Wformat=2 -Wformat-overflow=2 -Wformat-signedness -Wformat-truncation=2 -Wswitch-default -Wstringop-overflow=4 -Warray-bounds=2 -Wattribute-alias=2 -Wcatch-value=2 -Wplacement-new=2 -Wtrampolines -Winvalid-imported-macros -Winvalid-imported-macros")

		set(FGL_CONFIG "-std=c++20 -fmax-errors=3 -fconcepts-diagnostics-depth=4")
		set(FGL_DEBUG "-Og -g -fstrict-aliasing -fno-omit-frame-pointer -fstack-check -ftrapv -fverbose-asm")
		#Generates system specific stuff (IE requires AVX)
		set(FGL_SYSTEM_SPECIFIC "-march=native -fgcse -fgcse-las -fgcse-sm -fdeclone-ctor-dtor -fdevirtualize-speculatively -ftree-loop-im -fivopts -ftree-loop-ivcanon -fira-hoist-pressure -fsched-pressure -fsched-spec-load -fipa-pta -flto=auto -s -ffat-lto-objects -fno-enforce-eh-specs -fstrict-enums -funroll-loops")
		#Generates safe optimization flags
		set(FGL_SYSTEM_SAFE "-O3 -fdevirtualize-at-ltrans -s")
		set(FGL_FLAGS_DEBUG "${FGL_WARNINGS} ${FGL_CONFIG} ${FGL_DEBUG}")
		set(FGL_FLAGS_SYSTEM "${FLG_CONFIG} -DNDEBUG ${FGL_SYSTEM_SAFE} ${FGL_SYSTEM_SPECIFIC}")
		set(FGL_FLAGS_RELEASE "${FGL_CONFIG} -DNDEBUG -s ${FGL_SYSTEM_SAFE} ${FGL_WARNINGS} -Werror")
		set(FGL_FLAGS_RELWITHDEBINFO "${FGL_CONFIG} -DNDEBUG -g ${FGL_SYSTEM_SAFE} ${FGL_SYSTEM_SPECIFIC}")
		set(FGL_FLAGS "${FGL_FLAGS_${UPPER_BUILD_TYPE}}" PARENT_SCOPE)  # PARENT_SCOPE sends the variable up one level.
	endif ()
endfunction()

function(CompilerPostSetup)

endfunction()
