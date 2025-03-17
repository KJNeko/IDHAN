//
// Created by kj16609 on 3/13/25.
//
#pragma once

#include <expected>

#include "IDHANTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#include "namespaces.hpp"
#include "subtags.hpp"

namespace idhan
{

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	findOrCreateTag( NamespaceID namespace_id, SubtagID subtag_id, drogon::orm::DbClientPtr db );

}