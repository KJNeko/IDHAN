//
// Created by kj16609 on 6/28/22.
//

#include "subtags.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"


uint64_t addSubtag( const std::string& subtag )
{
	Database db;
	pqxx::work work { db.getWork() };

	const pqxx::result res = work.exec( "INSERT INTO subtags (subtag) VALUES ('" + subtag + "') RETURNING subtag_id" );
	work.commit();
	return res[ 0 ][ "subtag_id" ].as<uint64_t>();
}

std::string getSubtag( const uint64_t subtag_id )
{
	Database db;
	pqxx::work work { db.getWork() };

	const pqxx::result res = work.exec( "SELECT subtag FROM subtags WHERE subtag_id = " + std::to_string( subtag_id ) );
	if ( res.empty() )
	{
		throw EmptyReturn( "No subtag with id " + std::to_string( subtag_id ) + " found." );
	}
	work.commit();
	return res[ 0 ][ "subtag" ].as<std::string>();
}

uint64_t getSubtagID( const std::string& subtag, const bool create = false )
{
	Database db;
	pqxx::work work { db.getWork() };

	const pqxx::result res = work.exec( "SELECT subtag_id FROM subtags WHERE subtag = '" + subtag + "'" );

	if ( res.empty() )
	{
		if ( create )
		{
			work.commit();
			db.release();
			return addSubtag( subtag );
		}
		else
		{
			throw EmptyReturn( "No subtag with name " + subtag + " found." );
		}
	}
	work.commit();
	return res[ 0 ][ "subtag_id" ].as<uint64_t>();
}

void deleteSubtag( const std::string& subtag )
{
	Database db;
	pqxx::work work { db.getWork() };

	const pqxx::result res = work.exec( "DELETE FROM subtags WHERE subtag = '" + subtag + "' CASCADE" );
	if ( res.affected_rows() == 0 )
	{
		throw EmptyReturn( "No subtag with name " + subtag + " found to be deleted." );
	}
}

void deleteSubtag( const uint64_t subtag_id )
{
	deleteSubtag( getSubtag( subtag_id ) );
}