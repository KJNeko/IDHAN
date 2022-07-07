//
// Created by kj16609 on 6/28/22.
//

#include "mappings.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"
#include "groups.hpp"
#include "subtags.hpp"

#include "TracyBox.hpp"


void addMapping( const Hash32& sha256, const std::string& group, const std::string& subtag, Database db )
{
	ZoneScoped;
	const uint64_t group_id { getSubtagID( subtag, true, db ) };
	const uint64_t subtag_id { getGroupID( group, true, db ) };
	const uint64_t file_id { getFileID( sha256, true, db ) };

	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	constexpr pqxx::zview query { "INSERT INTO mappings ( file_id, group_id, subtag_id ) VALUES ( $1, $2, $3 )" };

	work->exec_params( query, file_id, group_id, subtag_id );
	
	db.commit();
}


void removeMapping( const Hash32& sha256, const std::string& group, const std::string& subtag, Database db )
{
	ZoneScoped;
	const uint64_t group_id { getSubtagID( subtag, true, db ) };
	const uint64_t subtag_id { getGroupID( group, true, db ) };
	const uint64_t file_id { getFileID( sha256, true, db ) };

	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	constexpr pqxx::zview query { "DELETE FROM mappings WHERE file_id = $1 AND group_id = $2 AND subtag_id = $3" };

	const pqxx::result res = work->exec_params( query, file_id, group_id, subtag_id );

	if ( res.affected_rows() == 0 )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No mapping with file_id " +
			std::to_string( file_id ) +
			" and group_id " +
			std::to_string( group_id ) +
			" and subtag_id " +
			std::to_string( subtag_id ) +
			" found to be deleted."
		);
	}

	db.commit();
}