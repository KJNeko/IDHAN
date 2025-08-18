//
// Created by kj16609 on 5/7/25.
//
#pragma once
#include <expected>
#include <vector>

#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"

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
#include "drogon/HttpAppFramework.h"
#include "drogon/orm/BaseBuilder.h"
#include "drogon/orm/DbClient.h"
#pragma GCC diagnostic pop

namespace idhan
{
class TagSearch
{
	drogon::orm::DbClientPtr m_db;
	TagDomainID m_domain;

	ExpectedTask< TagID > idealize( TagID id );

	std::vector< TagID > m_ids {};

	ExpectedTask< void > addChildren( TagID uint32 );

	ExpectedTask< std::vector< TagID > > findSiblings( TagID id );
	ExpectedTask< void > removeSiblings();

  public:

	TagSearch( TagDomainID tag_domain_id, drogon::orm::DbClientPtr db = drogon::app().getDbClient() );

	ExpectedTask< void > addID( TagID id );

	ExpectedTask< std::vector< RecordID > > search();
};
} // namespace idhan