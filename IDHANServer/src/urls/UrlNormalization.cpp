#include "UrlNormalization.hpp"

#include <algorithm>

#include "downloader/DownloadSessionManager.hpp"

namespace idhan::helpers
{

runtime::Task< std::expected< std::vector< std::string >, UrlNormalizationError > > normalizeUrls(
	std::vector< std::string > urls )
{
	if ( urls.empty() ) co_return std::vector< std::string > {};

	auto normalized_e { co_await downloader::downloadSessionManager().normalizeUrls( std::move( urls ) ) };
	if ( !normalized_e.has_value() )
		co_return std::unexpected(
			UrlNormalizationError { .failure = UrlNormalizationFailure::Unavailable,
			                        .message = "URL classes are unavailable: " + normalized_e.error() } );

	std::vector< std::string > normalized {};
	normalized.reserve( normalized_e.value().size() );
	for ( auto& url_e : normalized_e.value() )
	{
		if ( !url_e.has_value() )
			co_return std::unexpected(
				UrlNormalizationError { .failure = UrlNormalizationFailure::InvalidUrl, .message = url_e.error() } );

		normalized.emplace_back( std::move( url_e.value().url ) );
	}

	co_return normalized;
}

runtime::Task< std::expected< std::string, UrlNormalizationError > > normalizeUrl( std::string url )
{
	auto normalized_e { co_await normalizeUrls( { std::move( url ) } ) };
	if ( !normalized_e.has_value() ) co_return std::unexpected( std::move( normalized_e.error() ) );

	co_return std::move( normalized_e.value().front() );
}

runtime::Task< std::expected< std::vector< std::string >, UrlNormalizationError > > urlSpellings(
	std::vector< std::string > urls )
{
	if ( urls.empty() ) co_return std::vector< std::string > {};

	auto normalized_e { co_await downloader::downloadSessionManager().normalizeUrls( urls ) };
	if ( !normalized_e.has_value() )
		co_return std::unexpected(
			UrlNormalizationError { .failure = UrlNormalizationFailure::Unavailable,
			                        .message = "URL classes are unavailable: " + normalized_e.error() } );

	for ( auto& url_e : normalized_e.value() )
	{
		if ( url_e.has_value() ) urls.emplace_back( std::move( url_e.value().url ) );
	}

	std::ranges::sort( urls );
	const auto duplicates { std::ranges::unique( urls ) };
	urls.erase( duplicates.begin(), duplicates.end() );
	co_return urls;
}

} // namespace idhan::helpers
