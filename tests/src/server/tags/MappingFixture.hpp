//
// Created by kj16609 on 8/18/25.
//
#pragma once
#include "ServerTagFixture.hpp"

class MappingFixture : public ServerTagFixture
{
	RecordID default_record_id { 0 };

  protected:

	void createMapping( TagID tag_id );
	void deleteMapping( TagID tag_id );
	bool mappingExists( TagID tag_id );

	RecordID createRecord( const std::string_view data );
	void SetUp() override;
};
