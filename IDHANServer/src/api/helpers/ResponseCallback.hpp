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
using ResponseFunction = std::function< void( const drogon::HttpResponsePtr& ) >;

using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;
using Request = drogon::HttpRequestPtr;

} // namespace idhan
