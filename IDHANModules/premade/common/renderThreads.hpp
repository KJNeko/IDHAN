#pragma once

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace idhan::premade
{

//! Threads one module call may use internally, as the host sized it.
/** \return Zero when the host said nothing, in which case the backend's own default stands. */
inline std::size_t renderThreads()
{
	const char* const value { std::getenv( "IDHAN_RENDER_THREADS" ) };
	if ( value == nullptr ) return 0;

	const std::string_view text { value };

	std::size_t threads { 0 };
	const auto parsed { std::from_chars( text.data(), text.data() + text.size(), threads ) };

	if ( parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size() ) return 0;

	return threads;
}

} // namespace idhan::premade
