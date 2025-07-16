//
// Created by kj16609 on 3/13/25.
//
#pragma once

#include <expected>

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

#include "namespaces.hpp"
#include "subtags.hpp"

namespace idhan
{

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	findOrCreateTag( NamespaceID namespace_id, SubtagID subtag_id, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	findOrCreateTag( std::string tag_text, drogon::orm::DbClientPtr db );

} // namespace idhan