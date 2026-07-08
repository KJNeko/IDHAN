//
// Created by kj16609 on 4/18/25.
//
#pragma once
#include <charconv>
#include <string_view>

#include "drogon/HttpRequest.h"

namespace idhan
{

template < typename T >
std::vector< T > parseArrayParameters( drogon::HttpRequestPtr request, const std::string_view target )
{
	const std::string_view query { request->getQuery() };

	std::vector< T > vec {};

	for ( std::size_t i = 0; i < query.size(); )
	{
		const auto separator { query.find( '&', i ) };
		const auto param { query.substr( i, separator == std::string_view::npos ? std::string_view::npos : separator - i ) };
		i = separator == std::string_view::npos ? query.size() : separator + 1;

		// each parameter must be exactly `target=value`; a bare prefix test would also
		// match `target` embedded in longer parameter names
		if ( param.size() <= target.size() + 1 ) continue;
		if ( !param.starts_with( target ) || param[ target.size() ] != '=' ) continue;

		const auto value { param.substr( target.size() + 1 ) };

		if constexpr ( std::same_as< T, std::string > )
		{
			vec.emplace_back( value );
		}
		else if constexpr ( std::is_scalar_v< T > )
		{
			T number {};
			const auto [ ptr, ec ] = std::from_chars( value.data(), value.data() + value.size(), number );

			// coercing an unparsable value to 0 would silently query for id 0
			if ( ec != std::errc {} || ptr != value.data() + value.size() ) continue;

			vec.push_back( number );
		}
		else
			static_assert( false, "Array parser cannot handle type" );
	}

	return vec;
}

} // namespace idhan
