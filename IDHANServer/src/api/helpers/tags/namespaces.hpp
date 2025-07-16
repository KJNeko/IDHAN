//
// Created by kj16609 on 3/13/25.
//
#pragma once
#include <expected>
#include <string>

#include "IDHANTypes.hpp"

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
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#pragma GCC diagnostic pop

namespace idhan
{

drogon::Task< std::expected< NamespaceID, drogon::HttpResponsePtr > >
	findOrCreateNamespace( const std::string& str, drogon::orm::DbClientPtr db );

}
