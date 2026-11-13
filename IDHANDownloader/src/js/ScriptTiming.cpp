#include "js/ScriptTiming.hpp"

#include <array>
#include <string_view>

#include "logging/format_ns.hpp"

namespace idhan::downloader
{
std::string formatScriptDuration( const std::chrono::nanoseconds elapsed )
{
	using namespace std::chrono_literals;
	constexpr std::array< std::chrono::nanoseconds, 6 > units {
		{ 1ns,
		  std::chrono::nanoseconds { 1us },
		  std::chrono::nanoseconds { 1ms },
		  std::chrono::nanoseconds { 1s },
		  std::chrono::nanoseconds { 1min },
		  std::chrono::nanoseconds { 1h } }
	};
	constexpr std::array< std::string_view, 6 > suffixes { { "ns", "us", "ms", "s", "min", "h" } };
	std::size_t unit {};
	while ( unit + 1 < units.size() && elapsed >= units[ unit + 1 ] ) ++unit;

	const auto whole { elapsed.count() / units[ unit ].count() };
	const auto fraction { ( elapsed.count() % units[ unit ].count() ) * 10 / units[ unit ].count() };
	return format_ns::format( "{}.{}{}", whole, fraction, suffixes[ unit ] );
}
} // namespace idhan::downloader
