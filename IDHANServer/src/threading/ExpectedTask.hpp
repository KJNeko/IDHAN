#pragma once
#include <expected>

#include "IDHANTask.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"

namespace idhan::threading
{

//! The coroutine type used throughout the server.
template < typename T = void >
using Task = drogon::Task< T >;

//! A coroutine returning either a value of type \p T or an HTTP error response. Handlers co_await it
//! and use return_unexpected_error() to forward the error, or read .value() on success.
template < typename T = void >
using ExpectedTask = Task< std::expected< T, drogon::HttpResponsePtr > >;

//! The value-or-error-response pair carried by an ExpectedTask, usable as a plain (non-coroutine) type.
template < typename T = void >
using ExpectedResponse = std::expected< T, drogon::HttpResponsePtr >;

//! In a coroutine, co_returns the error response held by \p type when it is an unexpected. \p type
//! must be a std::expected (e.g. the result of co_awaiting an ExpectedTask helper).
#define return_unexpected_error( type )                                                                                \
	if ( !type ) co_return std::unexpected( type.error() );

#define return_optional_error( type )                                                                                  \
	if ( type ) co_return *type

} // namespace idhan::threading

namespace idhan
{

using namespace idhan::threading;

}