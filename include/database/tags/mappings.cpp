//
// Created by kj16609 on 6/28/22.
//

#include "mappings.hpp"
#include "database/database.hpp"
#include "database/utility/databaseExceptions.hpp"
#include "tags.hpp"

#include "TracyBox.hpp"


void addMapping( const uint64_t hash_id, const std::string& group, const std::string& subtag )
{

	ZoneScoped;
	const uint64_t tag_id { getTagID( group, subtag, true ) };

	const Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query { "INSERT INTO mappings ( hash_id, tag_id ) VALUES ( $1, $2 )" };

	work->exec_params( query, hash_id, tag_id );
}


void removeMapping( const uint64_t hash_id, const std::string& group, const std::string& subtag )
{
	ZoneScoped;
	const uint64_t tag_id { getTagID( group, subtag, true ) };

	const Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query { "DELETE FROM mappings WHERE hash_id = $1 AND tag_id = $1" };

	const pqxx::result res { work->exec_params( query, hash_id, tag_id ) };

	if ( res.affected_rows() == 0 )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No mapping found for " +
			std::to_string( hash_id ) +
			" in " +
			group +
			":" +
			subtag
		);
	}

}


void addMappingToHash( const Hash32& sha256, const std::string& group, const std::string& subtag )
{
	ZoneScoped;

	const Connection conn;
	auto work { conn.getWork() };

	const uint64_t tag_id { getTagID( group, subtag, true ) };
	const uint64_t hash_id { getFileID( sha256, true ) };

	//Check that it wasn't made before we locked
	constexpr pqxx::zview checkMapping { "SELECT * FROM mappings WHERE tag_id = $1 AND hash_id = $2" };
	const pqxx::result check_ret { work->exec_params( checkMapping, tag_id, hash_id ) };
	if ( check_ret.size() )
	{
		return;
	}

	addMapping( hash_id, group, subtag );
}


void removeMappingFromHash( const Hash32& sha256, const std::string& group, const std::string& subtag )
{
	const uint64_t hash_id { getFileID( sha256, false ) };

	removeMapping( hash_id, group, subtag );
}