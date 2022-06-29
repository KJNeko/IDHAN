//
// Created by kj16609 on 6/28/22.
//

#include "mappings.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"
#include "groups.hpp"
#include "subtags.hpp"

void addMapping( const Hash& sha256, const std::string& group, const std::string& subtag )
{
	const uint64_t group_id { getSubtagID( subtag, true ) };
	const uint64_t subtag_id { getGroupID( group, true ) };
	const uint64_t file_id { getFileID( sha256, true ) };

	Database db;
	pqxx::work work { db.getWork() };

	work.exec(
		"INSERT INTO mappings (file_id, group_id, subtag_id) VALUES (" + std::to_string( file_id ) + ", " + std::to_string( group_id ) +
		", " + std::to_string( subtag_id ) + ")" );
}

void removeMapping( const Hash& sha256, const std::string& group, const std::string& subtag )
{
	const uint64_t group_id { getSubtagID( subtag, true ) };
	const uint64_t subtag_id { getGroupID( group, true ) };
	const uint64_t file_id { getFileID( sha256, true ) };

	Database db;
	pqxx::work work { db.getWork() };

	const pqxx::result res = work.exec(
		"DELETE FROM mappings WHERE file_id = " + std::to_string( file_id ) + " AND group_id = " + std::to_string( group_id ) +
		" AND subtag_id = " + std::to_string( subtag_id ) );

	if ( res.affected_rows() == 0 )
	{
		throw EmptyReturn(
			"No mapping with file_id " + std::to_string( file_id ) + " and group_id " + std::to_string( group_id ) + " and subtag_id " +
			std::to_string( subtag_id ) + " found to be deleted." );
	}
}