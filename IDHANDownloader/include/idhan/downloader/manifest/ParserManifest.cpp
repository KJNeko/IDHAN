#include "ParserManifest.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

ParserManifest::ParserManifest( const element json ) :
  m_format_version { read< std::uint32_t >( json, "formatVersion" ) },
  m_sdk_version { read< std::string >( json, "sdkVersion" ) },
  m_id { read< std::string >( json, "id" ) }, m_version { read< std::uint64_t >( json, "version" ) },
  m_options { list< ParserOption >( json, "options", true ) },
  m_handlers { list< ParserHandler >( json, "handlers", true ) },
  m_discoveries { list< ParserDiscovery >( json, "discoveries", true ) },
  m_cookies { list< ParserCookie >( json, "cookies" ) }, m_url_classes { list< URLClass >( json, "urlClasses", true ) }
{
	if ( m_format_version != 1 ) invalid( "formatVersion", "unsupported manifest version" );

	spdlog::info( "Parser manifest {} v{} (format {}, SDK {}): {} options, {} handlers, {} discovery hooks, {} cookies, {} URL classes",
		m_id, m_version, m_format_version, m_sdk_version, m_options.size(), m_handlers.size(),
		m_discoveries.size(), m_cookies.size(), m_url_classes.size() );
}
