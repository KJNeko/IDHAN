//
// Created by kj16609 on 10/27/25.
//
#pragma once
#include <memory>

#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "logging/format_ns.hpp"

namespace idhan
{

class ErrorInfo : public std::enable_shared_from_this< ErrorInfo >
{
	std::string m_message;
	drogon::HttpStatusCode code { drogon::HttpStatusCode::k500InternalServerError };

  public:

	ErrorInfo( const std::string str ) : m_message( str ) {}

	std::shared_ptr< ErrorInfo > setCode( const drogon::HttpStatusCode code );

	drogon::HttpResponsePtr genResponse() const;

	// TODO: If stacktrace is available, mark where this is called
	std::shared_ptr< ErrorInfo > error();
};

using IDHANError = std::shared_ptr< ErrorInfo >;

template < typename... Args >
IDHANError createError( const format_ns::format_string< Args... > str, Args&&... args )
{
	return std::make_shared< ErrorInfo >( format_ns::format( str, args... ) );
}

} // namespace idhan
