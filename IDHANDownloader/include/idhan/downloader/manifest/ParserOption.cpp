#include "ParserOption.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

ParserOption::ParserOption( element json ) :
  m_name { read< std::string >( json, "name" ) }, m_description { read< std::string >( json, "description" ) }
{
	const auto type { read< std::string >( json, "type" ) };
	const auto value { required( json, "default" ) };
	if ( type == "boolean" )
	{
		m_type = Type::BOOLEAN;
		m_default = decode< bool >( value );
	}
	else if ( type == "number" )
	{
		m_type = Type::NUMBER;
		const auto number { decode< double >( value ) };
		m_minimum = optional< double >( json, "minimum" );
		m_integer = optional< bool >( json, "integer" ).value_or( false );
		if ( m_minimum && number < m_minimum.value() ) invalid( m_name, "default is below minimum" );

		if ( m_integer && ( std::trunc( number ) != number || std::abs( number ) > static_cast< double >( js_max_safe_integer ) ) )
		{
			invalid( m_name, "default must be a safe integer" );
		}

		m_default = number;
	}
	else if ( type == "string" )
	{
		m_type = Type::STRING;
		m_default = decode< std::string >( value );
	}
	else if ( type == "string[]" )
	{
		m_type = Type::STRING_ARRAY;
		m_default = array< std::string >( value );
	}
	else
	{
		invalid( m_name, "unsupported option type" );
	}
}
