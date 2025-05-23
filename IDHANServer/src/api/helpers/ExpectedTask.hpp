//
// Created by kj16609 on 5/7/25.
//
#pragma once
#include <expected>

#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"

template < typename T >
using ExpectedTask = drogon::Task< std::expected< T, drogon::HttpResponsePtr > >;
