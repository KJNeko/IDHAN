#pragma once
#include <expected>

#include "IDHANTask.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"

namespace idhan::threading
{

template < typename T = void >
using Task = drogon::Task< T >;

//! Coroutine result that either holds a value or an HTTP error response.
template < typename T = void >
using ExpectedTask = Task< std::expected< T, drogon::HttpResponsePtr > >;

template < typename T = void >
using ExpectedResponse = std::expected< T, drogon::HttpResponsePtr >;

#define return_unexpected_error( type )                                                                                \
	if ( !type ) co_return std::unexpected( type.error() );

#define return_optional_error( type )                                                                                  \
	if ( type ) co_return *type

} // namespace idhan::threading

namespace idhan
{

using namespace idhan::threading;

}
