//
// Created by kj16609 on 10/27/25.
//
#pragma once
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <IDHANTypes.hpp>
#include <expected>

#include "errors/ErrorInfo.hpp"

namespace idhan
{

drogon::Task< std::optional< NamespaceID > > findNamespace( std::string, drogon::orm::DbClientPtr db );
drogon::Task< std::optional< SubtagID > > findSubtag( std::string, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< NamespaceID, IDHANError > > createNamespace( std::string, drogon::orm::DbClientPtr db );
drogon::Task< std::expected< SubtagID, IDHANError > > createSubtag( std::string, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< TagID, IDHANError > >
	createTag( std::string tag_namespace, std::string tag_subtag, drogon::orm::DbClientPtr db );

} // namespace idhan