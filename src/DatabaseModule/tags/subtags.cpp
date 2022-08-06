//
// Created by kj16609 on 6/28/22.
//

#include "subtags.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"


namespace subtags
{

	uint64_t createSubtag( const Subtag& subtag )
	{
		constexpr pqxx::zview checkSubtag { "SELECT subtag_id FROM subtags WHERE subtag = $1" };

		constexpr pqxx::zview query { "INSERT INTO subtags (subtag) VALUES ($1) RETURNING subtag_id" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		//Check that it wasn't made before we locked
		const pqxx::result check_ret { work->exec_params( checkSubtag, subtag ) };
		if ( check_ret.size() )
		{
			return check_ret[ 0 ][ "subtag_id" ].as< uint64_t >();
		}


		const pqxx::result res { work->exec_params( query, subtag ) };

		return res[ 0 ][ "subtag_id" ].as< uint64_t >();

	}


	Subtag getSubtag( const uint64_t subtag_id )
	{
		constexpr pqxx::zview query { "SELECT subtag FROM subtags WHERE subtag_id = $1" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result res { work->exec_params( query, subtag_id ) };

		if ( res.empty() )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, ( "No subtag with ID " + std::to_string( subtag_id ) + " found." )
			);
		}

		return res[ 0 ][ "subtag" ].as< std::string >();
	}


	uint64_t getSubtagID( const Subtag& subtag )
	{
		constexpr pqxx::zview query { "SELECT subtag_id FROM subtags WHERE subtag = $1" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		const pqxx::result res { work->exec_params( query, subtag ) };

		if ( res.empty() )
		{
			spdlog::error( "No subtag with name {} found. create == false", subtag );
			throw IDHANError( ErrorNo::DATABASE_DATA_NOT_FOUND, "No subtag with name " + subtag + " found." );
		}


		return res[ 0 ][ "subtag_id" ].as< uint64_t >();
	}


	void deleteSubtag( const Subtag& subtag )
	{
		constexpr pqxx::zview query { "DELETE FROM subtags WHERE subtag = $1 CASCADE" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		const pqxx::result res { work->exec_params( query, subtag ) };

		if ( res.affected_rows() == 0 )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "No subtag with name " + subtag + " found to be deleted."
			);
		}
	}


	void deleteSubtag( const uint64_t subtag_id )
	{
		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		constexpr pqxx::zview query { "DELETE FROM subtags WHERE subtag_id = $1 CASCADE" };

		const pqxx::result res { work->exec_params( query, subtag_id ) };

		if ( res.affected_rows() == 0 )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "No subtag with ID " +
				std::to_string( subtag_id ) +
				" found to be deleted."
			);
		}
	}
}