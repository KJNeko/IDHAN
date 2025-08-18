//
// Created by kj16609 on 8/18/25.
//

#include "MappingFixture.hpp"

void MappingFixture::createMapping( TagID tag_id )
{
	pqxx::work tx { *conn };

	tx.exec_params( "INSERT INTO tag_mappings (tag_id, record_id, tag_domain_id) VALUES ($1, $2, $3)", pqxx::params { tag_id, default_record_id, default_domain_id } );

	tx.commit();
}

void MappingFixture::deleteMapping( TagID tag_id )
{
	pqxx::work tx { *conn };

	tx.exec_params( "DELETE FROM tag_mappings WHERE tag_id = $1 AND record_id = $2 AND tag_domain_id = $3", pqxx::params { tag_id, default_record_id, default_domain_id } );

	tx.commit();
}

bool MappingFixture::mappingExists( TagID tag_id )
{
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "SELECT EXISTS(SELECT 1 FROM tag_mappings WHERE tag_id = $1 AND record_id = $2 AND tag_domain_id = $3)", pqxx::params { tag_id, default_record_id, default_domain_id } ) };

	tx.commit();

	return result[ 0 ][ 0 ].as< bool >();
}

RecordID MappingFixture::createRecord( const std::string_view data )
{
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "INSERT INTO records (sha256) VALUES (digest($1, 'sha256')) RETURNING record_id", pqxx::params { data } ) };

	tx.commit();

	if ( result.empty() ) throw std::runtime_error( "Failed to create record" );

	return result[ 0 ][ 0 ].as< RecordID >();
}

void MappingFixture::SetUp()
{
	ServerTagFixture::SetUp();

	default_record_id = createRecord( "test" );
}
