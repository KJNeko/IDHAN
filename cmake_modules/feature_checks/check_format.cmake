

check_cxx_source_compiles(
		"#include <format>\nint main(void){ std::format(\"{}\", 1);return 0;}"
		HAVE_STD_FORMAT
)


