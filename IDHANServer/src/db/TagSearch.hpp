//
// Created by kj16609 on 5/7/25.
//
#pragma once
#include <expected>
#include <vector>

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/orm/BaseBuilder.h"
#include "drogon/orm/DbClient.h"

namespace idhan
{
class TagSearch
{
	drogon::orm::DbClientPtr m_db;
	TagDomainID m_domain;

	drogon::Task< std::expected< unsigned, std::shared_ptr< drogon::HttpResponse > > > idealize( TagID id );

	std::vector< TagID > m_ids {};

	ExpectedTask< void > addChildren( TagID uint32 );

	ExpectedTask< std::vector< TagID > > findSiblings( TagID id );
	ExpectedTask< void > removeSiblings();

  public:

	TagSearch( TagDomainID domain_id, drogon::orm::DbClientPtr db = drogon::app().getDbClient() );

	ExpectedTask< void > addID( TagID id );

	ExpectedTask< std::vector< RecordID > > search();
};
} // namespace idhan