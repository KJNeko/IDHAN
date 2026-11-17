#include "Parameter.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

StringParameter::StringParameter( std::string key, element json ) :
  ParameterBase { std::move( key ), json },
  m_default { idhan::downloader::manifest_json::optional< std::string >( json, "default" ).value_or( "" ) },
  m_pattern { idhan::downloader::manifest_json::optional< std::string >( json, "pattern" ).value_or( "" ) }
{
	if ( read< std::string >( json, "type" ) != "string" ) invalid( m_key, "expected string parameter" );
}

IntegralParameter::IntegralParameter( std::string key, element json ) : ParameterBase { std::move( key ), json }
{
	constexpr std::int64_t safe { js_max_safe_integer };
	m_default = idhan::downloader::manifest_json::optional< std::int64_t >( json, "default" );
	max = idhan::downloader::manifest_json::optional< std::int64_t >( json, "maximum" ).value_or( safe );
	min = idhan::downloader::manifest_json::optional< std::int64_t >( json, "minimum" ).value_or( -safe );
	if ( read< std::string >( json, "type" ) != "integer" ) invalid( m_key, "expected integer parameter" );

	if ( min < -safe || max > safe || min > max ) invalid( m_key, "invalid safe integer bounds" );

	if ( m_default && ( m_default.value() < min || m_default.value() > max ) )
	{
		invalid( m_key, "default is outside bounds" );
	}
}
