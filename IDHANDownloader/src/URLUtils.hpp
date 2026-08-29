#pragma once

#include <ada.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace idhan::downloader::detail
{

inline std::string normalizeHost( std::string host )
{
	// strip leading protocol
	if ( auto protocol_end = host.find_first_of( "://" ); protocol_end != std::string::npos )
	{
		host = host.substr( protocol_end + 3 );
	}

	std::ranges::transform(
		host,
		host.begin(),
		[]( const unsigned char character ) { return static_cast< char >( std::tolower( character ) ); } );

	return host;
}

inline bool isSubdomainOf( const std::string_view host, const std::string_view domain )
{
	if ( host.size() <= domain.size() ) return false;
	if ( !host.ends_with( domain ) ) return false;

	return host[ host.size() - domain.size() - 1 ] == '.';
}

inline bool hostMatches( const std::string_view host, const std::string_view domain, const bool include_subdomains )
{
	if ( host == domain ) return true;

	return include_subdomains && isSubdomainOf( host, domain );
}

inline std::string redactUrlQuery( const std::string_view url, const std::vector< std::string >& names )
{
	if ( names.empty() ) return std::string { url };

	auto parsed { ada::parse< ada::url_aggregator >( url ) };

	if ( !parsed ) return "<redacted URL>";

	ada::url_search_params parameters { parsed->get_search() };

	for ( const std::string& name : names )
	{
		if ( parameters.has( name ) ) parameters.set( name, "<redacted>" );
	}

	parsed->set_search( parameters.to_string() );
	return std::string { parsed->get_href() };
}

} // namespace idhan::downloader::detail
