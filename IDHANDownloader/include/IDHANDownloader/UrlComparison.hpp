#pragma once

#include <spdlog/spdlog.h>

#include <ada.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace idhan::downloader
{

// Keep the path intact: changing path escapes or case can select a different resource.
[[nodiscard]] inline std::string urlComparisonBase( const std::string_view url )
{
	return std::string { url.substr( 0, url.find_first_of( "?#" ) ) };
}

// Sort only the query's fields. Preserve their original encoding, values, repeated-name
// order and the fragment so stored signed URLs can still be used for requests.
[[nodiscard]] inline std::string sortUrlQueryParameters( const std::string_view url )
{
	std::string sorted { url };
	const auto fragment { url.find( '#' ) };
	const auto query { url.find( '?' ) };
	if ( query == std::string_view::npos || query > fragment ) return sorted;

	const auto end { fragment == std::string_view::npos ? url.size() : fragment };
	std::vector< std::pair< std::string, std::string_view > > fields {};
	std::size_t start { query + 1 };
	while ( start <= end )
	{
		const auto separator { url.find( '&', start ) };
		const auto field_end { std::min( separator, end ) };
		const auto field { url.substr( start, field_end - start ) };
		if ( !field.empty() )
		{
			ada::url_search_params parameter { "name=" + std::string { field.substr( 0, field.find( '=' ) ) } };
			const auto name { parameter.get( "name" ) };
			fields.emplace_back( name ? std::string { *name } : std::string {}, field );
		}
		if ( field_end == end ) break;

		start = field_end + 1;
	}
	std::ranges::stable_sort( fields, {}, &decltype( fields )::value_type::first );
	sorted.resize( query + 1 );
	for ( std::size_t index {}; index < fields.size(); ++index )
	{
		if ( index != 0 ) sorted += '&';
		sorted += fields[ index ].second;
	}
	if ( fragment != std::string_view::npos ) sorted += url.substr( fragment );
	return sorted;
}

// This key is only for comparisons. Storage sorting retains all query parameters.
// Stable sorting preserves the order of repeated values for the same parameter name.
[[nodiscard]] inline std::string urlComparisonKey(
	const std::string_view url,
	const std::vector< std::string >& ephemeral_query = {} )
{
	const auto without_fragment { url.substr( 0, url.find( '#' ) ) };
	const auto query { without_fragment.find( '?' ) };
	std::string key { without_fragment.substr( 0, query ) };
	if ( query == std::string_view::npos ) return key;

	ada::url_search_params parameters { without_fragment.substr( query + 1 ) };
	for ( const auto& name : ephemeral_query ) parameters.remove( name );
	parameters.sort();
	const auto normalized_query { parameters.to_string() };
	if ( !normalized_query.empty() ) key += "?" + normalized_query;

	spdlog::debug( "{} normalized to {}", url, key );

	return key;
}

} // namespace idhan::downloader
