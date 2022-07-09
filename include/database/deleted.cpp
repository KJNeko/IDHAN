//
// Created by kj16609 on 7/1/22.
//

#include "deleted.hpp"


bool deleted( const uint64_t hash_id )
{

	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT hash_id FROM deleted WHERE hash_id = $1" };

	const pqxx::result res { work.exec_params( query, hash_id ) };

	work.commit();

	return !res.empty();
}


bool deleted( const Hash32 sha256 )
{

	const uint64_t id = getFileID( sha256, false );

	if ( id == 0 )
	{
		return false;
	}

	return deleted( id );
}
