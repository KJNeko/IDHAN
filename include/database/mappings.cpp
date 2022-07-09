//
// Created by kj16609 on 6/28/22.
//

#include "mappings.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"
#include "tags.hpp"

#include "TracyBox.hpp"


void addMapping( const Hash32& sha256, const std::string& group, const std::string& subtag )
{
	ZoneScoped;

	Connection conn;
	pqxx::work work { conn() };

	const uint64_t tag_id { getTagID( group, subtag, true ) };
	const uint64_t hash_id { getFileID( sha256, true ) };

	constexpr pqxx::zview query { "INSERT INTO mappings ( hash_id, tag_id ) VALUES ( $1, $2 )" };

	work.exec_params( query, hash_id, tag_id );

	work.commit();
}


void removeMapping( const Hash32& sha256, const std::string& group, const std::string& subtag )
{
	ZoneScoped;
	const uint64_t tag_id { getTagID( group, subtag, false ) };
	const uint64_t hash_id { getFileID( sha256, false ) };


	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "DELETE FROM mappings WHERE hash_id = $1 AND tag_id = $1" };

	const pqxx::result res = work.exec_params( query, hash_id, tag_id );

	if ( res.affected_rows() == 0 )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No mapping found for " +
			sha256.getQByteArray().toHex().toStdString() +
			" in " +
			group +
			":" +
			subtag
		);
	}

	work.commit();
}