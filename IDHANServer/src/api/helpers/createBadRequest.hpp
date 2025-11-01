//
// Created by kj16609 on 3/6/25.
//
#pragma once
#include <format>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/HttpResponse.h"
#pragma GCC diagnostic pop

#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan
{

namespace internal
{
drogon::HttpResponsePtr createBadResponse( const std::string& message, drogon::HttpStatusCode code );
} // namespace internal

template < typename... Args >
drogon::HttpResponsePtr createBadRequest( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k400BadRequest );
}

inline drogon::HttpResponsePtr createBadRequest( const std::string& msg )
{
	log::warn( msg );
	return internal::createBadResponse( msg, drogon::HttpStatusCode::k400BadRequest );
}

template < typename... Args >
drogon::HttpResponsePtr createNotFound( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k404NotFound );
}

template < typename... Args >
drogon::HttpResponsePtr createInternalError( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::error( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k500InternalServerError );
}

inline drogon::HttpResponsePtr createInternalError( const std::string& msg )
{
	log::error( msg );
	return internal::createBadResponse( msg, drogon::HttpStatusCode::k500InternalServerError );
}

template < typename... Args >
drogon::HttpResponsePtr createConflict( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k409Conflict );
}

} // namespace idhan
