//
// Created by kj16609 on 10/27/25.
//
#pragma once
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <IDHANTypes.hpp>

namespace idhan
{

drogon::Task< std::optional< NamespaceID > > findNamespace( std::string, drogon::orm::DbClientPtr db );
drogon::Task< std::optional< SubtagID > > findSubtag( std::string, drogon::orm::DbClientPtr db );

drogon::Task< NamespaceID > createNamespace( std::string, drogon::orm::DbClientPtr db );
drogon::Task< SubtagID > createSubtag( std::string, drogon::orm::DbClientPtr db );

drogon::Task< TagID > createTag( std::string tag_namespace, std::string tag_subtag, drogon::orm::DbClientPtr db );

} // namespace idhan