//
// Created by kj16609 on 6/28/22.
//

#include "groups.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"

#include "TracyBox.hpp"


uint64_t addGroup( const Group& group )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "INSERT INTO groups (group_name) VALUES ($1) RETURNING group_id" };

	const pqxx::result res = work.exec_params( query, group.text );

	work.commit();


	return res[ 0 ][ "group_id" ].as< uint64_t >();
}


Group getGroup( const uint64_t group_id )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT group_name FROM groups WHERE group_id = $1" };

	const pqxx::result res = work.exec_params( query, group_id );

	if ( res.empty() )
	{
		spdlog::error( "No group with ID {} found.", group_id );
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with id " + std::to_string( group_id ) + " found."
		);
	}

	work.commit();

	return res[ 0 ][ "group" ].as< std::string >();
}


uint64_t getGroupID( const Group& group, const bool create )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT group_id FROM groups WHERE group_name = $1" };

	const pqxx::result res { work.exec_params( query, group.text ) };

	if ( res.empty() )
	{
		if ( create )
		{
			work.commit();
			return addGroup( group );
		}
		else
		{
			work.abort();
			spdlog::error( "No group with name {} found. create == false", group.text );
			throw IDHANError( ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with name " + group.text + " found." );
		}
	}

	work.commit();

	return res[ 0 ][ "group_id" ].as< uint64_t >();
}


void removeGroup( const Group& group )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "DELETE FROM groups WHERE group_name = $1" };

	const pqxx::result res { work.exec_params( query, group.text ) };

	if ( res.affected_rows() == 0 )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with name " + group.text + " found to be deleted."
		);
	}

	work.commit();
}


void removeGroup( const uint64_t group_id )
{
	ZoneScoped;
	removeGroup( getGroup( group_id ) );
}