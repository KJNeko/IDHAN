#pragma once

#include "JsonDecodingValues.hpp"
#include "Parameter.hpp"
#include "RequestRate.hpp"

namespace idhan::downloader::manifest_json
{
inline RequestRatePolicy rate( element json )
{
	const auto value { field( json, "requestRate" ) };
	if ( !value ) return std::monostate {};

	if ( value->is_bool() ) return RequestRateDisabled { value.value() };

	return RequestRate { value.value() };
}

inline std::vector< Parameter > parameters( element json )
{
	std::vector< Parameter > result {};
	for ( const auto entry : object( json ) )
	{
		const auto type { read< std::string >( entry.value, "type" ) };
		if ( type == "string" )
		{
			result.emplace_back( StringParameter { std::string { entry.key }, entry.value } );
		}
		else if ( type == "integer" )
		{
			result.emplace_back( IntegralParameter { std::string { entry.key }, entry.value } );
		}
		else
		{
			invalid( entry.key, "unsupported parameter type" );
		}
	}

	return result;
}
}

