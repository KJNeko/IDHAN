#pragma once

#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

namespace idhan::downloader
{

inline constexpr unsigned int required_sdk_major { 1 };
inline constexpr unsigned int required_sdk_minor { 0 };
inline constexpr unsigned int required_sdk_patch { 0 };

// Accept releases of the same major version at or above the minimum required release.
inline bool compatibleParserSDKVersion( const std::string_view version )
{
	std::array< unsigned int, 3 > parts {};
	auto remaining { version };

	for ( std::size_t i {}; i < parts.size(); ++i )
	{
		const auto separator { remaining.find( '.' ) };
		if ( i < 2 && separator == std::string_view::npos ) return false;
		if ( i == 2 && separator != std::string_view::npos ) return false;

		const auto component { remaining.substr( 0, separator ) };
		if ( component.empty() || ( component.size() > 1 && component.front() == '0' ) ) return false;

		const auto parsed { std::from_chars( component.data(), component.data() + component.size(), parts[ i ] ) };
		if ( parsed.ec != std::errc {} || parsed.ptr != component.data() + component.size() ) return false;

		if ( i < 2 ) remaining.remove_prefix( separator + 1 );
	}

	if ( parts[ 0 ] != required_sdk_major ) return false;

	return std::array { parts[ 1 ], parts[ 2 ] } >= std::array { required_sdk_minor, required_sdk_patch };
}

} // namespace idhan::downloader
