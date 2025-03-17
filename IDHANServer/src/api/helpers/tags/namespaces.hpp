//
// Created by kj16609 on 3/13/25.
//
#pragma once
#include <expected>
#include <string>

#include "IDHANTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"

namespace idhan
{

drogon::Task< std::expected< NamespaceID, drogon::HttpResponsePtr > >
	findOrCreateNamespace( const std::string& str, drogon::orm::DbClientPtr db );

}
