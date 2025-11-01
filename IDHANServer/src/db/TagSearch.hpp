//
// Created by kj16609 on 5/7/25.
//
#pragma once
#include <expected>
#include <vector>

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "dbTypes.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/orm/BaseBuilder.h"
#include "drogon/orm/DbClient.h"

namespace idhan
{
class TagSearch
{
	DbClientPtr m_db;
	TagDomainID m_domain;

	ExpectedTask< TagID > idealize( TagID id );

	std::vector< TagID > m_ids {};

	ExpectedTask< void > addChildren( TagID uint32 );

	ExpectedTask< std::vector< TagID > > findSiblings( TagID id );

	ExpectedTask< void > removeSiblings();

  public:

	TagSearch( TagDomainID tag_domain_id, DbClientPtr db = drogon::app().getDbClient() );

	ExpectedTask< void > addID( TagID id );

	ExpectedTask< std::vector< RecordID > > search();
};
} // namespace idhan
