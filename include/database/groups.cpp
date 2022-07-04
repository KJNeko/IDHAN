//
// Created by kj16609 on 6/28/22.
//

#include "groups.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"

#include "TracyBox.hpp"


uint64_t addGroup( const std::string& group, Database db )
{
	ZoneScoped;
	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	const pqxx::result res = work->exec(
		"INSERT INTO groups (group_name) VALUES ('" + work->esc( group ) + "') RETURNING group_id"
	);

	db.commit();

	return res[ 0 ][ "group_id" ].as< uint64_t >();
}


std::string getGroup( const uint64_t group_id, Database db )
{
	ZoneScoped;
	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	const pqxx::result res = work->exec(
		"SELECT group_name FROM groups WHERE group_id = " + std::to_string( group_id )
	);
	if ( res.empty() )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with id " + std::to_string( group_id ) + " found."
		);
	}

	db.commit();

	return res[ 0 ][ "group" ].as< std::string >();
}


uint64_t getGroupID( const std::string& group, const bool create, Database db )
{
	ZoneScoped;
	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	const pqxx::result res = work->exec(
		"SELECT group_id FROM groups WHERE group_name = '" + work->esc( group ) + "'"
	);
	if ( res.empty() )
	{
		if ( create )
		{ return addGroup( group, db ); }
		else
		{
			throw IDHANError( ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with name " + group + " found." );
		}
	}

	db.commit();

	return res[ 0 ][ "group_id" ].as< uint64_t >();
}


void removeGroup( const std::string& group, Database db )
{
	ZoneScoped;
	std::shared_ptr< pqxx::work > work { db.getWorkPtr() };

	const pqxx::result res = work->exec( "DELETE FROM groups WHERE group_name = '" + group + "' CASCADE" );
	if ( res.affected_rows() == 0 )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with name " + group + " found to be deleted."
		);
	}

	db.commit();
}


void removeGroup( const uint64_t group_id, Database db )
{
	ZoneScoped;
	removeGroup( getGroup( group_id, db ), db );

	db.commit();
}