//
// Created by kj16609 on 3/6/25.
//
#pragma once
#include <format>

#include "drogon/HttpResponse.h"

namespace idhan
{

namespace internal
{
drogon::HttpResponsePtr createBadResponse( const std::string& message, drogon::HttpStatusCode code );
} // namespace internal

template < typename... Args >
drogon::HttpResponsePtr createBadRequest( const format_ns::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k400BadRequest );
}

template < typename... Args >
inline drogon::HttpResponsePtr createNotFound( const format_ns::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k404NotFound );
}

template < typename... Args >
drogon::HttpResponsePtr createInternalError( const format_ns::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k500InternalServerError );
}

template < typename... Args >
drogon::HttpResponsePtr createConflict( const format_ns::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k409Conflict );
}

} // namespace idhan