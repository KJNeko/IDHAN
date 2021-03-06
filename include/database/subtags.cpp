//
// Created by kj16609 on 6/28/22.
//

#include "subtags.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"

#include "TracyBox.hpp"


uint64_t addSubtag( const std::string& subtag )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "INSERT INTO subtags (subtag) VALUES ($1) RETURNING subtag_id" };

	const pqxx::result res { work.exec_params( query, subtag ) };

	work.commit();

	return res[ 0 ][ "subtag_id" ].as< uint64_t >();
}


std::string getSubtag( const uint64_t subtag_id )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT subtag FROM subtags WHERE subtag_id = $1" };

	const pqxx::result res { work.exec_params( query, subtag_id ) };

	if ( res.empty() )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, ( "No subtag with ID " + std::to_string( subtag_id ) + " found." )
		);
	}

	work.commit();

	return res[ 0 ][ "subtag" ].as< std::string >();
}


uint64_t getSubtagID( const std::string& subtag, const bool create )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT subtag_id FROM subtags WHERE subtag = $1" };

	const pqxx::result res { work.exec_params( query, subtag ) };

	if ( res.empty() )
	{
		if ( create )
		{
			work.commit();
			return addSubtag( subtag );
		}
		else
		{
			work.abort();
			spdlog::error( "No subtag with name {} found. create == false", subtag );
			throw IDHANError( ErrorNo::DATABASE_DATA_NOT_FOUND, "No subtag with name " + subtag + " found." );
		}
	}

	work.commit();

	return res[ 0 ][ "subtag_id" ].as< uint64_t >();
}


void deleteSubtag( const std::string& subtag )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "DELETE FROM subtags WHERE subtag = $1 CASCADE" };

	const pqxx::result res { work.exec_params( query, subtag ) };

	if ( res.affected_rows() == 0 )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No subtag with name " + subtag + " found to be deleted."
		);
	}

	work.commit();
}


void deleteSubtag( const uint64_t subtag_id )
{
	ZoneScoped;
	deleteSubtag( getSubtag( subtag_id ) );
}