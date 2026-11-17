#include "Pattern.hpp"

#include <spdlog/spdlog.h>

#include <ada.h>

#include "JsonDecoding.hpp"

using namespace idhan::downloader::manifest_json;

AdaPattern initAdaPattern( element json )
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

SearchPattern initSearchPattern( element json )
{
	// json["query"]
	const auto search_params { field( json, "query" ) };
	SearchPattern pattern {};
	if ( !search_params ) return pattern;

	return pattern;
}

Pattern::Pattern( element json ) :
  m_ada_pattern( initAdaPattern( json ) ),
  m_search_pattern( initSearchPattern( json ) )
{}

bool Pattern::match( std::string_view url )
{
	// split after ?
	auto url_start { url.subview( 0, url.find( '?' ) ) };
	spdlog::info( "Splitting {}", url );

	return m_ada_pattern.match( url_start ) && this->matchQuery( url );
}

bool Pattern::matchQuery( std::string_view url )
{
	const auto query { url.subview( url.find( '?' ) ) };
	ada::url_search_params params { query };

	std::size_t found_count { 0 };

	for ( const auto& [ key, value, capture ] : this->m_search_pattern.m_queries )
	{
		const auto found { params.get_all( key ) };

		if ( found.empty() ) return false; // Early eject, We're missing an expected parameter

		//TODO: Add in ability to ask for specific size of parameters
		// if(found.size() > N)

		// If we are not a capture we want the value to be exact. Ex: `gelbooru?s=list`
		if ( !capture && found[ 0 ] != value ) return false;
	}

	if ( found_count == this->m_search_pattern.m_queries.size() ) return true;

	return false;
}

std::unordered_map< std::string, std::string > Pattern::captures( std::string_view url )
{}
