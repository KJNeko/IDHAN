#include "IDHANDownloader/urls/UrlNormalization.hpp"

#include <algorithm>
#include <format>

namespace idhan::downloader
{

struct QueryField
{
	std::string name {};
	std::string_view text {};
};

static std::string queryFieldName( const std::string_view field )
{
	ada::url_search_params parameter { "name=" + std::string { field.substr( 0, field.find( '=' ) ) } };
	const auto name { parameter.get( "name" ) };
	if ( !name.has_value() ) return {};

	return std::string { name.value() };
}

static void appendField( std::string& query, const std::string_view field )
{
	if ( !query.empty() ) query += '&';

	query += field;
}

std::string withDefaultScheme( const std::string_view url )
{
	if ( url.starts_with( "//" ) ) return "https:" + std::string { url };

	// Only a "://" before the path counts, e.g. not the one in "example.com/r?to=https://x.com".
	// Requiring it also stops "example.com:8080/post" from parsing as the scheme "example.com".
	const auto separator { url.find( "://" ) };
	const bool has_scheme { separator != std::string_view::npos && separator < url.find_first_of( "/?#" ) };
	if ( !has_scheme ) return "https://" + std::string { url };

	return std::string { url };
}

std::expected< ada::url_aggregator, std::string > parseUrl( const std::string_view url )
{
	auto parsed { ada::parse< ada::url_aggregator >( withDefaultScheme( url ) ) };
	if ( !parsed ) return std::unexpected( std::format( "Invalid URL: {}", url ) );

	return std::move( parsed.value() );
}

ada::url_aggregator collapsePathSlashes( ada::url_aggregator url )
{
	if ( url.has_opaque_path ) return url;

	const std::string_view path { url.get_pathname() };
	if ( !path.contains( "//" ) ) return url;

	std::string collapsed {};
	collapsed.reserve( path.size() );
	for ( const char character : path )
	{
		const bool repeated_slash { character == '/' && collapsed.ends_with( '/' ) };
		if ( repeated_slash ) continue;

		collapsed += character;
	}

	url.set_pathname( collapsed );
	return url;
}

NormalizedUrl applyUrlRules( ada::url_aggregator url, const UrlRules& rules )
{
	const std::string search { url.get_search() };
	const std::string_view query { search.empty() ? std::string_view {} : std::string_view { search }.substr( 1 ) };

	std::vector< QueryField > fields {};
	std::string ephemeral {};
	std::size_t start {};
	while ( start <= query.size() )
	{
		const auto field_end { std::min( query.find( '&', start ), query.size() ) };
		const auto field { query.substr( start, field_end - start ) };
		start = field_end + 1;

		if ( field.empty() ) continue;

		auto name { queryFieldName( field ) };
		if ( std::ranges::contains( rules.remove_query, name ) ) continue;

		if ( std::ranges::contains( rules.ephemeral_query, name ) )
		{
			appendField( ephemeral, field );
			continue;
		}

		fields.emplace_back( std::move( name ), field );
	}

	if ( rules.sort_query ) std::ranges::stable_sort( fields, {}, &QueryField::name );

	std::string normalized_query {};
	for ( const auto& field : fields ) appendField( normalized_query, field.text );

	// set_search strips one leading '?', which would otherwise eat a field such as "?a=1".
	url.set_search( normalized_query.empty() ? normalized_query : "?" + normalized_query );
	return NormalizedUrl { .url = std::string { url.get_href() }, .ephemeral_query = std::move( ephemeral ) };
}

} // namespace idhan::downloader
