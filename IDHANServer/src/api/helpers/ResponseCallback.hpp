//
// Created by kj16609 on 8/11/24.
//

#pragma once

#include <functional>

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
#include "drogon/HttpAppFramework.h"
#pragma GCC diagnostic pop

namespace idhan
{
//! Callback invoked with the HTTP response once a handler has produced it.
using ResponseFunction = std::function< void( const drogon::HttpResponsePtr& ) >;

//! Coroutine return type for API handlers: co_returns the HTTP response to send back.
using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;
//! Shorthand for an incoming HTTP request pointer.
using Request = drogon::HttpRequestPtr;

} // namespace idhan
