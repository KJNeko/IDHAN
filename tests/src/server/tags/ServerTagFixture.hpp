//
// Created by kj16609 on 8/18/25.
//
#pragma once

#include <gtest/gtest.h>
#include <pqxx/pqxx>

#include "IDHANTypes.hpp"
#include "ServerDBFixture.hpp"

using namespace idhan;

class ServerTagFixture : public ServerDBFixture
{
  protected:

	void SetUp() override;

	TagDomainID createDomain( std::string_view name ) const;

	TagID createTag( std::string_view text ) const;

	void createAlias( TagID aliased_id, TagID alias_id );

	bool aliasExists( TagID aliased_id, TagID alias_id );

	void createParent( TagID parent_id, TagID child_id );

	bool parentExists( TagID parent_id, TagID child_id );

	testing::AssertionResult parentInternalExists( RecordID record_id, TagID parent_id, TagID child_id, std::uint32_t count = 1 );

	TagDomainID default_domain_id { 0 };
};
