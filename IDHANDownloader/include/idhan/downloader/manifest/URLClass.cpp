#include "URLClass.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

URLClass::URLClass( element json ) :
  m_id { read< std::string >( json, "id" ) }, m_version { read< std::uint64_t >( json, "version" ) },
  m_hosts { list< URLClassHost >( json, "hosts", true ) }, m_request_rate { rate( json ) },
  m_parser { optional< std::string >( json, "parser" ) },
  m_url_patterns { list< URLPattern >( json, "url_patterns" ) },
  m_generators { list< URLGenerator >( json, "generators" ) },
  m_sort_query_parameters { optional< bool >( json, "sortQueryParameters" ).value_or( true ) },
  m_remove_query { list< std::string >( json, "removeQuery" ) },
  m_ephemeral_query { list< std::string >( json, "ephemeralQuery" ) }
{
	spdlog::debug( "URL class {} v{}: {} hosts, {} URL rules, {} generators; parser={}",
		m_id, m_version, m_hosts.size(), m_url_patterns.size(), m_generators.size(), m_parser.value_or( "none" ) );
}
