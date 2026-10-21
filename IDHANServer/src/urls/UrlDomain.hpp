#pragma once

#include <string>

namespace idhan::helpers
{

//! Extracts the bare hostname from a URL string (strips protocol, path, port).
[[nodiscard]] inline std::string extractDomain( const std::string& url )
{
	const auto protocol_end { url.find( "://" ) };
	const auto protocol { url.substr( 0, protocol_end ) };

	if ( protocol == "file" ) return "";

	const auto start_pos { protocol_end != std::string::npos ? protocol_end + 3 : 0 };
	const auto authority_end { url.find_first_of( "/?#", start_pos ) };
	std::string authority {
		authority_end != std::string::npos ?
			url.substr( start_pos, authority_end - start_pos ) :
			url.substr( start_pos )
	};

	const auto user_info_end { authority.rfind( '@' ) };
	if ( user_info_end != std::string::npos ) authority.erase( 0, user_info_end + 1 );

	if ( !authority.empty() && authority[ 0 ] == '[' )
	{
		// IPv6 literal: "[::1]:8080" → "::1"
		const auto close_bracket { authority.find( ']' ) };
		return close_bracket != std::string::npos ? authority.substr( 1, close_bracket - 1 ) : authority.substr( 1 );
	}

	const auto port_pos { authority.find( ':' ) };
	if ( port_pos != std::string::npos ) authority.erase( port_pos );

	return authority;
}

} // namespace idhan::helpers
