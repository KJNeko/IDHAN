//
// Created by kj16609 on 4/18/25.
//
#pragma once
#include <string_view>

#include "drogon/HttpRequest.h"

namespace idhan
{

template < typename T >
std::vector< T > parseArrayParmeters( drogon::HttpRequestPtr request, const std::string_view target )
{
	const auto query { request->getQuery() };

	std::vector< T > vec {};

	for ( std::size_t i = 0; i < query.size(); ++i )
	{
		// OOB check
		if ( i >= query.size() - target.size() ) break;

		// test if the current index is what we are looking for.
		if ( query.substr( i, target.size() ) != target ) continue;

		const auto end { query.find_first_of( '&', i ) };
		const auto start_val { query.find_first_of( '=', i ) + 1 };
		const auto diff { end - start_val };
		const std::string_view remainder { query.data() + start_val, diff };

		i += target.size();

		if constexpr ( std::same_as< T, std::string > )
		{
			vec.push_back( remainder );
		}
		else if constexpr ( std::is_scalar_v< T > )
		{
			T number {};
			std::from_chars( remainder.data(), remainder.data() + remainder.size(), number );
			vec.push_back( number );
			// log::info(
			// 	"Found parameter {} was value {} extracted from {}",
			// 	target,
			// 	number,
			// 	std::string_view( remainder.data(), remainder.data() + remainder.size() ) );
		}
		else
			static_assert( false, "Array parser cannot handle type" );
	}

	return vec;
}

} // namespace idhan
