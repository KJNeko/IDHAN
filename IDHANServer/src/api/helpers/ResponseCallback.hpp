//
// Created by kj16609 on 8/11/24.
//

#pragma once

#include <functional>

#include "drogon/HttpAppFramework.h"

namespace idhan
{
using ResponseFunction = std::function< void( const drogon::HttpResponsePtr& ) >;

using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;
using Request = drogon::HttpRequestPtr;

} // namespace idhan