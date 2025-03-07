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

drogon::HttpResponsePtr createBadRequest( const Json::Value& json );

template < typename... Args >
drogon::HttpResponsePtr createBadRequest( const std::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse( std::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k400BadRequest);
}

template < typename... Args >
drogon::HttpResponsePtr createInternalError( const std::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse( std::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k500InternalServerError );
}

template < typename... Args >
drogon::HttpResponsePtr createConflict( const std::format_string< Args... > str, Args&&... args )
{
	return internal::createBadResponse( std::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k409Conflict );
}

} // namespace idhan