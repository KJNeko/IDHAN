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

	TagID getIdealAliasId( TagID aliased_id );
	TagID getIdealAliasIdRaw( TagID aliased_id );
	TagID getAliasEffectiveTagId( TagID aliased_id );
	void removeAlias( TagID aliased_id );
	void removeParent( TagID parent_id, TagID child_id );

	std::size_t getTagStorageCount( TagID tag_id );
	std::size_t getTagDisplayCount( TagID tag_id );
	bool tagCountsExist( TagID tag_id );

	bool activeMappingExists( RecordID record_id, TagID tag_id );
	TagID getActiveMappingIdeal( RecordID record_id, TagID tag_id );
	std::size_t countMappingsForRecord( RecordID record_id );

	std::size_t countParentMappings( RecordID record_id );
	bool parentMappingExists( RecordID record_id, TagID tag_id, TagID origin_id );

	bool finalViewMappingExists( RecordID record_id, TagID tag_id );
	std::size_t countFinalViewMappings( RecordID record_id );

	std::vector< TagID > getChildrenForParent( TagID parent_id );
	std::vector< TagID > getParentsForChild( TagID child_id );

	TagDomainID default_domain_id { 0 };
};
