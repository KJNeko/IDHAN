//
// Created by kj16609 on 7/1/22.
//

#include "deleted.hpp"


bool deleted( uint64_t hash_id, Database db )
{

	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	pqxx::result res = work->exec(
		"SELECT hash_id FROM deleted WHERE hash_id = " + std::to_string( hash_id )
	);

	db.commit();

	return !res.empty();
}


bool deleted( Hash32 sha256, Database db )
{
	try
	{
		uint64_t id = getFileID( sha256, false, db );
		db.commit();
		return deleted( id, db );
	}
	catch ( ... )
	{
		db.commit();
		return false;
	}
}
