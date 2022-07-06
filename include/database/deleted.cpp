//
// Created by kj16609 on 7/1/22.
//

#include "deleted.hpp"


bool deleted( const uint64_t hash_id, Database db )
{

	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	constexpr pqxx::zview query { "SELECT hash_id FROM deleted WHERE hash_id = $1" };

	const pqxx::result res { work->exec_params( query, hash_id ) };

	db.commit();

	return !res.empty();
}


bool deleted( const Hash32 sha256, Database db )
{

	const uint64_t id = getFileID( sha256, false, db );

	if ( id == 0 )
	{
		return false;
	}

	db.commit();
	return deleted( id, db );
}
