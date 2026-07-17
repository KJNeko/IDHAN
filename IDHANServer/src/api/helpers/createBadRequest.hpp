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
//! Shared back-end for the create* helpers: builds an HttpResponse carrying \p message with status
//! \p code. Prefer the status-specific wrappers below over calling this directly.
drogon::HttpResponsePtr createBadResponse( const std::string& message, drogon::HttpStatusCode code );
} // namespace internal

//! Logs a warning and returns a 400 Bad Request whose body is the std::format-formatted message.
template < typename... Args >
drogon::HttpResponsePtr createBadRequest( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k400BadRequest );
}

//! \copydoc createBadRequest
inline drogon::HttpResponsePtr createBadRequest( const std::string& msg )
{
	log::warn( msg );
	return internal::createBadResponse( msg, drogon::HttpStatusCode::k400BadRequest );
}

//! Logs a warning and returns a 404 Not Found whose body is the formatted message.
template < typename... Args >
drogon::HttpResponsePtr createNotFound( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k404NotFound );
}

//! Logs a warning and returns a 500 Internal Server Error whose body is the formatted message.
template < typename... Args >
drogon::HttpResponsePtr createInternalError( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k500InternalServerError );
}

//! \copydoc createInternalError
inline drogon::HttpResponsePtr createInternalError( const std::string& msg )
{
	log::warn( msg );
	return internal::createBadResponse( msg, drogon::HttpStatusCode::k500InternalServerError );
}

//! Logs a warning and returns a 409 Conflict whose body is the formatted message.
template < typename... Args >
drogon::HttpResponsePtr createConflict( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k409Conflict );
}

//! Logs a warning and returns a 501 Not Implemented whose body is the formatted message.
template < typename... Args >
drogon::HttpResponsePtr createNotImplemented( const format_ns::format_string< Args... > str, Args&&... args )
{
	log::warn( format_ns::format( str, std::forward< Args >( args )... ) );
	return internal::createBadResponse(
		format_ns::format( str, std::forward< Args >( args )... ), drogon::HttpStatusCode::k501NotImplemented );
}

} // namespace idhan
