//
// Created by kj16609 on 5/7/25.
//
#pragma once
#include <expected>

#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"

namespace idhan::threading
{
template < typename T = void >
using ExpectedTask = drogon::Task< std::expected< T, drogon::HttpResponsePtr > >;

#define return_unexpected_error( type )                                                                                \
	if ( !type ) co_return std::unexpected( type.error() );
} // namespace idhan::threading

namespace idhan
{

using namespace idhan::threading;

}