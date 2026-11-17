#include "Pattern.hpp"

#include <spdlog/spdlog.h>

#include <ada.h>

#include "JsonDecoding.hpp"

using namespace idhan::downloader::manifest_json;

static AdaPattern initAdaPattern( const element json )
{
	ada::url_pattern_init init {};

	init.protocol = optional< std::string >( json, "protocol" );
	init.username = optional< std::string >( json, "username" );
	init.password = optional< std::string >( json, "password" );
	init.hostname = optional< std::string >( json, "hostname" );
	init.port = optional< std::string >( json, "port" );
	init.pathname = optional< std::string >( json, "pathname" );
	init.search = optional< std::string >( json, "search" );
	init.hash = optional< std::string >( json, "hash" );
	init.base_url = optional< std::string >( json, "baseURL" );

	auto pattern_e { ada::parse_url_pattern< ada::url_pattern_regex::std_regex_provider >(
		std::variant< std::string_view, ada::url_pattern_init > { std::move( init ) } ) };

	if ( !pattern_e.has_value() )
	{
		throw std::runtime_error( "Invalid URL pattern in manifest" );
	}

	const auto& pattern { pattern_e.value() };

	spdlog::trace(
		"URL pattern: hostname='{}', pathname='{}', search='{}'",
		pattern.get_hostname(),
		pattern.get_pathname(),
		pattern.get_search() );

	return std::move( pattern_e.value() );
}

static SearchPattern initSearchPattern( const element json )
{
	// json["query"]
	const auto search_params { field( json, "query" ) };
	SearchPattern pattern {};
	if ( !search_params ) return pattern;

	for ( const auto entry : object( search_params.value() ) )
	{
		pattern.m_queries.emplace_back( std::string { entry.key }, entry.value );
	}

	return pattern;
}

Pattern::Pattern( const element json ) :
  m_ada_pattern( initAdaPattern( json ) ),
  m_search_pattern( initSearchPattern( json ) )
{}

bool Pattern::match( const std::string_view url )
{
	auto url_e { ada::parse< ada::url_aggregator >( url ) };
	if ( !url_e.has_value() )
	{
		return false;
	}

	return tryMatch( url_e.value() ).has_value();
}

bool Pattern::matchQuery( const std::string_view url ) const
{
	auto url_e { ada::parse< ada::url_aggregator >( url ) };
	if ( !url_e.has_value() )
	{
		return false;
	}

	return m_search_pattern.tryMatch( url_e.value().get_search() ).has_value();
}

std::optional< URLCaptures > SearchPattern::tryMatch( const std::string_view search ) const
{
	ada::url_search_params params { search };
	URLCaptures captures {};
	for ( const auto& query : m_queries )
	{
		const auto value { params.get( query.m_key ) };
		if ( !value.has_value() )
		{
			return std::nullopt;
		}

		if ( query.m_capture )
		{
			captures.emplace( query.m_value, std::string { value.value() } );
			continue;
		}

		if ( value.value() != query.m_value )
		{
			return std::nullopt;
		}
	}

	return captures;
}

std::optional< URLCaptures > Pattern::tryMatch( const ada::url_aggregator& url )
{
	auto match_e { m_ada_pattern.exec( std::string_view { url.get_href() } ) };
	if ( !match_e.has_value() )
	{
		return std::nullopt;
	}

	const auto& match { match_e.value() };
	if ( !match.has_value() )
	{
		return std::nullopt;
	}

	auto captures { m_search_pattern.tryMatch( url.get_search() ) };
	if ( !captures.has_value() )
	{
		return std::nullopt;
	}

	const auto& result { match.value() };
	for ( const auto* component :
	      { &result.protocol,
	        &result.username,
	        &result.password,
	        &result.hostname,
	        &result.port,
	        &result.pathname,
	        &result.search,
	        &result.hash } )
	{
		for ( const auto& [ name, value ] : component->groups )
		{
			if ( value.has_value() )
			{
				captures.value().emplace( name, value.value() );
			}
		}
	}

	return captures;
}

std::unordered_map< std::string, std::string > Pattern::captures( std::string_view url )
{
	auto url_e { ada::parse< ada::url_aggregator >( url ) };
	if ( !url_e.has_value() )
	{
		return {};
	}

	return tryMatch( url_e.value() ).value_or( URLCaptures {} );
}
