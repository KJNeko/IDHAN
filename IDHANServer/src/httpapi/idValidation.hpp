#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <simdjson.h>
#include <string_view>

#include "idhan/http/Response.hpp"

namespace idhan::httpapi
{

template < typename Id >
[[nodiscard]] std::expected< Id, http::Response > positiveId( const std::int64_t value, const std::string_view name )
{
	if ( value <= 0 )
		return std::unexpected( http::Response::error( 400, std::format( "{} must be a positive integer", name ) ) );

	if ( value > std::numeric_limits< Id >::max() )
		return std::unexpected( http::Response::error( 400, std::format( "{} is out of range", name ) ) );

	return static_cast< Id >( value );
}

template < typename Id >
[[nodiscard]] std::expected< Id, http::Response > parsePositiveId(
	const simdjson::dom::element element,
	const std::string_view name )
{
	std::int64_t value {};
	if ( element.get_int64().get( value ) != simdjson::SUCCESS )
		return std::unexpected( http::Response::error( 400, std::format( "{} must be an integer", name ) ) );

	return positiveId< Id >( value, name );
}

} // namespace idhan::httpapi
