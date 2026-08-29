#include "http/HttpMessage.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <charconv>

#include "logging/format_ns.hpp"

namespace idhan::downloader
{

static bool headerNamesEqual( const std::string_view left, const std::string_view right )
{
	return std::ranges::equal(
		left,
		right,
		[]( const unsigned char first, const unsigned char second )
		{ return std::tolower( first ) == std::tolower( second ); } );
}

static std::string_view trimmed( std::string_view text )
{
	while ( !text.empty() && ( text.front() == ' ' || text.front() == '\t' ) ) text.remove_prefix( 1 );
	while ( !text.empty()
	        && ( text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n' ) )
		text.remove_suffix( 1 );
	return text;
}

static std::string lowered( const std::string_view text )
{
	std::string result {};
	result.reserve( text.size() );

	for ( const char character : text )
		result.push_back( static_cast< char >( std::tolower( static_cast< unsigned char >( character ) ) ) );

	return result;
}

bool isMarkupContentType( std::string_view content_type )
{
	if ( const auto parameters { content_type.find( ';' ) }; parameters != std::string_view::npos )
		content_type = content_type.substr( 0, parameters );

	const std::string type { lowered( trimmed( content_type ) ) };

	return type == "text/html" || type == "application/xhtml+xml";
}

std::string describeBody( const std::string_view body, const std::uint64_t bytes )
{
	if ( bytes == 0 ) return "<empty>";
	if ( bytes > HTTP_BODY_LOG_LIMIT || body.size() < bytes ) return format_ns::format( "<{} bytes>", bytes );

	std::string collapsed {};
	collapsed.reserve( body.size() );
	bool pending_space { false };

	for ( const char character : body )
	{
		if ( std::isspace( static_cast< unsigned char >( character ) ) != 0 )
		{
			pending_space = !collapsed.empty();
			continue;
		}

		if ( pending_space ) collapsed.push_back( ' ' );

		pending_space = false;
		collapsed.push_back( character );
	}

	return collapsed;
}

std::optional< HttpMethod > parseHttpMethod( const std::string_view method )
{
	std::string upper {};
	upper.reserve( method.size() );
	for ( const char character : method )
		upper.push_back( static_cast< char >( std::toupper( static_cast< unsigned char >( character ) ) ) );

	if ( upper == "GET" ) return HttpMethod::GET;
	if ( upper == "POST" ) return HttpMethod::POST;
	if ( upper == "HEAD" ) return HttpMethod::HEAD;
	if ( upper == "PUT" ) return HttpMethod::PUT;
	if ( upper == "DELETE" ) return HttpMethod::DELETE;
	if ( upper == "OPTIONS" ) return HttpMethod::OPTIONS;
	if ( upper == "PATCH" ) return HttpMethod::PATCH;
	return std::nullopt;
}

std::string_view httpMethodName( const HttpMethod method )
{
	switch ( method )
	{
		case HttpMethod::GET:
			return "GET";
		case HttpMethod::POST:
			return "POST";
		case HttpMethod::HEAD:
			return "HEAD";
		case HttpMethod::PUT:
			return "PUT";
		case HttpMethod::DELETE:
			return "DELETE";
		case HttpMethod::OPTIONS:
			return "OPTIONS";
		case HttpMethod::PATCH:
			return "PATCH";
	}

	return "GET";
}

void HttpHeaders::add( std::string name, std::string value )
{
	m_entries.emplace_back( std::move( name ), std::move( value ) );
}

void HttpHeaders::set( std::string name, std::string value )
{
	erase( name );
	add( std::move( name ), std::move( value ) );
}

std::size_t HttpHeaders::erase( const std::string_view name )
{
	return std::erase_if( m_entries, [ name ]( const auto& entry ) { return headerNamesEqual( entry.first, name ); } );
}

bool HttpHeaders::contains( const std::string_view name ) const
{
	return std::ranges::any_of(
		m_entries, [ name ]( const auto& entry ) { return headerNamesEqual( entry.first, name ); } );
}

std::string_view HttpHeaders::get( const std::string_view name ) const
{
	const auto found { std::ranges::find_if(
		m_entries, [ name ]( const auto& entry ) { return headerNamesEqual( entry.first, name ); } ) };
	return found == m_entries.end() ? std::string_view {} : std::string_view { found->second };
}

std::vector< std::string_view > HttpHeaders::all( const std::string_view name ) const
{
	std::vector< std::string_view > output {};

	for ( const auto& [ entry_name, value ] : m_entries )
		if ( headerNamesEqual( entry_name, name ) ) output.emplace_back( value );

	return output;
}

bool isValidHttpHeaderName( const std::string_view name )
{
	if ( name.empty() ) return false;

	constexpr std::string_view separators { "\"(),/:;<=>?@[\\]{}" };

	for ( const char character : name )
	{
		const auto value { static_cast< unsigned char >( character ) };

		if ( value <= 0x20 || value >= 0x7F ) return false;
		if ( separators.find( character ) != std::string_view::npos ) return false;
	}

	return true;
}

bool isValidHttpHeaderValue( const std::string_view value )
{
	for ( const char character : value )
	{
		const auto raw { static_cast< unsigned char >( character ) };

		if ( raw == '\t' ) continue;
		if ( raw < 0x20 || raw == 0x7F ) return false;
	}

	return true;
}

std::optional< SetCookie > parseSetCookie( const std::string_view header )
{
	const auto pair_end { header.find( ';' ) };
	const std::string_view pair { trimmed( header.substr( 0, pair_end ) ) };
	const auto separator { pair.find( '=' ) };

	if ( separator == std::string_view::npos ) return std::nullopt;

	SetCookie cookie {};
	cookie.name = std::string { trimmed( pair.substr( 0, separator ) ) };
	cookie.value = std::string { trimmed( pair.substr( separator + 1 ) ) };

	if ( cookie.name.empty() ) return std::nullopt;

	std::string_view remainder {
		pair_end == std::string_view::npos ? std::string_view {} : header.substr( pair_end + 1 )
	};

	while ( !remainder.empty() )
	{
		const auto attribute_end { remainder.find( ';' ) };
		const std::string_view attribute { trimmed( remainder.substr( 0, attribute_end ) ) };
		remainder = attribute_end == std::string_view::npos ?
		                std::string_view {} :
		                remainder.substr( attribute_end + 1 );

		if ( attribute.empty() ) continue;

		const auto attribute_separator { attribute.find( '=' ) };
		const std::string_view name { trimmed( attribute.substr( 0, attribute_separator ) ) };
		const std::string_view value {
			attribute_separator == std::string_view::npos ?
				std::string_view {} :
				trimmed( attribute.substr( attribute_separator + 1 ) )
		};

		if ( headerNamesEqual( name, "domain" ) )
			cookie.domain = std::string { value };
		else if ( headerNamesEqual( name, "path" ) )
			cookie.path = std::string { value };
		else if ( headerNamesEqual( name, "secure" ) )
			cookie.secure = true;
		else if ( headerNamesEqual( name, "httponly" ) )
			cookie.http_only = true;
		else if ( headerNamesEqual( name, "max-age" ) )
		{
			std::int64_t seconds {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), seconds ) };
			if ( result.ec == std::errc {} ) cookie.max_age = seconds;
		}
		else if ( headerNamesEqual( name, "expires" ) )
		{
			const std::string date { value };
			const auto parsed { curl_getdate( date.c_str(), nullptr ) };
			if ( parsed != -1 )
				cookie.expires = std::chrono::system_clock::time_point { std::chrono::seconds { parsed } };
		}
	}

	return cookie;
}

} // namespace idhan::downloader
