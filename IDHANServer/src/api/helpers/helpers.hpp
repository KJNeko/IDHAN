//
// Created by kj16609 on 3/11/25.
//
#pragma once

#include <expected>
#include <vector>

#include "IDHANTypes.hpp"
#include "drogon/HttpResponse.h"

namespace idhan::api::helpers
{

std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainID( drogon::HttpRequestPtr request );

void addFileCacheHeader( drogon::HttpResponsePtr sharedPtr );

std::string pgEscape( const std::string& s );

template < typename T >
std::string pgArrayify( const std::vector< T >& vec )
{
	std::string data { "{" };
	data.reserve( vec.size() * 8 );

	std::size_t counter { 0 };

	for ( const auto& v : vec )
	{
		if constexpr ( std::same_as< T, std::string > )
		{
			data += pgEscape( v );
		}
		else if constexpr ( std::is_integral_v< T > )
		{
			data += std::to_string( v );
		}
		else
			static_assert( false, "Unknown type for pgArraify" );

		if ( counter < vec.size() - 1 )
		{
			data += ",";
		}

		counter += 1;
	}

	data += "}";

	return data;
}

} // namespace idhan::api::helpers
