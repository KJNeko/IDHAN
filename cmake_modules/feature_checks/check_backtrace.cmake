

check_cxx_source_compiles(
		"#include <backtrace>\nint main(void){ std::basic_stacktrace thing { std::basic_stacktrace::current() }; return 0;}"
		HAVE_STD_BACKTRACE
)