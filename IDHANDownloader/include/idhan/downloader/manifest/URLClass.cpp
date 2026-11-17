#include "URLClass.hpp"

#include <spdlog/spdlog.h>

#include "JsonDecoding.hpp"

using namespace idhan::downloader::manifest_json;

std::optional< URLMatch > URLClass::tryMatch( const ada::url_aggregator& url )
{
	bool host_matches {};
	for ( const auto& host : m_hosts )
	{
		if ( host.name == url.get_hostname() )
		{
			host_matches = true;
			break;
		}
	}

	if ( !host_matches )
	{
		return std::nullopt;
	}

	for ( auto& pattern : m_url_patterns )
	{
		auto captures { pattern.tryMatch( url ) };
		if ( captures.has_value() )
		{
			return URLMatch { m_id, pattern.m_name, pattern.m_generator, std::move( captures.value() ) };
		}
	}

	return std::nullopt;
}

URLClass::URLClass( element json ) :
  m_id { read< std::string >( json, "id" ) },
  m_version { read< std::uint64_t >( json, "version" ) },
  m_hosts { list< URLClassHost >( json, "hosts", true ) },
  m_request_rate { rate( json ) },
  m_parser { optional< std::string >( json, "parser" ) },
  m_url_patterns { list< URLPattern >( json, "url_patterns" ) },
  m_generators { list< URLGenerator >( json, "generators" ) },
  m_sort_query_parameters { optional< bool >( json, "sortQueryParameters" ).value_or( true ) },
  m_remove_query { list< std::string >( json, "removeQuery" ) },
  m_ephemeral_query { list< std::string >( json, "ephemeralQuery" ) }
{
	spdlog::debug(
		"URL class {} v{}: {} hosts, {} URL rules, {} generators; parser={}",
		m_id,
		m_version,
		m_hosts.size(),
		m_url_patterns.size(),
		m_generators.size(),
		m_parser.value_or( "none" ) );
}
