#include "idhan/http/RequestParameters.hpp"

#include <folly/String.h>

namespace idhan::http
{

static std::expected< std::string, ParseError > decode( const std::string_view encoded )
{
	std::string decoded {};
	if ( !folly::tryUriUnescape( encoded, decoded, folly::UriEscapeMode::QUERY ) )
		return std::unexpected( ParseError { "query", "has an invalid percent escape" } );

	if ( decoded.find( '\0' ) != std::string::npos )
		return std::unexpected( ParseError { "query", "must not contain NUL" } );

	return decoded;
}

std::expected< RequestParameters, ParseError > RequestParameters::parseQuery( const std::string_view query )
{
	RequestParameters parameters {};
	std::size_t start {};
	while ( start < query.size() )
	{
		const auto end { query.find( '&', start ) };
		const auto field { query.substr( start, end == std::string_view::npos ? query.size() - start : end - start ) };
		if ( field.empty() )
		{
			start = end + 1;
			continue;
		}
		const auto separator { field.find( '=' ) };
		auto name { decode( field.substr( 0, separator ) ) };
		if ( !name ) return std::unexpected( std::move( name.error() ) );

		auto value {
			decode( separator == std::string_view::npos ? std::string_view {} : field.substr( separator + 1 ) )
		};
		if ( !value ) return std::unexpected( std::move( value.error() ) );

		parameters.append( std::move( *name ), std::move( *value ) );
		if ( end == std::string_view::npos ) break;

		start = end + 1;
	}
	return parameters;
}

void RequestParameters::append( std::string name, std::string value )
{
	m_values.emplace_back( std::move( name ), std::move( value ) );
}

std::span< const std::pair< std::string, std::string > > RequestParameters::values() const noexcept
{
	return m_values;
}
} // namespace idhan::http
