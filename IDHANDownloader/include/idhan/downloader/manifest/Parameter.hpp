#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <simdjson.h>
#include <string>
#include <variant>

#include "JsonDecodingValues.hpp"

struct ParameterBase
{
	ParameterBase( std::string key, simdjson::dom::element json ) :
	  m_name { idhan::downloader::manifest_json::read< std::string >( json, "name" ) },
	  m_key { std::move( key ) },
	  optional { idhan::downloader::manifest_json::optional< bool >( json, "optional" ).value_or( false ) }
	{}

	std::string m_name;
	std::string m_key;
	bool optional { false };
};

struct StringParameter : ParameterBase
{
	StringParameter( std::string key, simdjson::dom::element json );

	//! An empty string means no default.
	std::string m_default {};
	//! empty if no pattern specified
	std::string m_pattern;
};

struct IntegralParameter : ParameterBase
{
	IntegralParameter( std::string key, simdjson::dom::element json );

	std::optional< std::int64_t > m_default { std::nullopt };
	std::int64_t max { std::numeric_limits< std::int64_t >::max() };
	std::int64_t min { std::numeric_limits< std::int64_t >::min() };
};

using Parameter = std::variant< StringParameter, IntegralParameter >;
