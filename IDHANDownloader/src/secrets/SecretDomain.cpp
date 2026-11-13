#include "IDHANDownloader/secrets/SecretDomain.hpp"

#include <ada.h>

namespace idhan::downloader
{

std::expected< std::string, std::string > secretDomain( const std::string_view domain )
{
	if ( domain.empty() ) return std::unexpected( "Secret domains must not be empty" );

	if ( domain.find_first_of( "/\\?#@" ) != std::string_view::npos )
		return std::unexpected( "Secret domains must be bare hostnames" );

	const auto parsed_e { ada::parse< ada::url_aggregator >( "https://" + std::string { domain } + "/" ) };

	if ( !parsed_e.has_value() ) return std::unexpected( "Invalid secret domain" );

	const auto& parsed { parsed_e.value() };

	// Reject explicit ports including default ports, which Ada otherwise removes.
	if ( domain.find( ':' ) != std::string_view::npos && !domain.ends_with( ']' ) )
		return std::unexpected( "Secret domains must not include a port" );

	std::string hostname { parsed.get_hostname() };

	if ( hostname.ends_with( '.' ) ) hostname.pop_back();

	if ( hostname.empty() ) return std::unexpected( "Invalid secret domain" );

	return hostname;
}

} // namespace idhan::downloader
