//
// Created by kj16609 on 6/28/22.
//

#include <QCache>
#include "groups.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"


namespace groups
{
	uint64_t createGroup( const Group& group )
	{
		constexpr pqxx::zview select_group_id_query { "SELECT group_id FROM groups WHERE group_name = $1" };
		constexpr pqxx::zview insert_group_query { "INSERT INTO groups (group_name) VALUES ($1) RETURNING group_id" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		const pqxx::result check_result { work->exec_params( select_group_id_query, group ) };
		if ( check_result.size() )
		{
			return check_result[ 0 ][ "group_id" ].as< uint64_t >();
		}


		const pqxx::result insert_result { work->exec_params( insert_group_query, group ) };

		return insert_result[ 0 ][ "group_id" ].as< uint64_t >();
	}


	Group getGroup( const uint64_t group_id )
	{
		constexpr pqxx::zview select_group_name_query { "SELECT group_name FROM groups WHERE group_id = $1" };


		ZoneScoped;
		constexpr uint64_t BPerMB { 1000000 };
		constexpr uint64_t size { 64 * BPerMB };

		static QCache< uint64_t, Group > group_cache { size };

		if ( group_cache.contains( group_id ) )
		{
			return *group_cache.object( group_id );
		}

		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result select_result { work->exec_params( select_group_name_query, group_id ) };

		if ( select_result.empty() )
		{
			spdlog::error( "No group with ID {} found.", group_id );
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with id " + std::to_string( group_id ) + " found."
			);
		}

		const std::string group_str { select_result[ 0 ][ "group_name" ].as< std::string_view >() };

		group_cache.insert( group_id, new Group( group_str ), group_str.size() );

		return select_result[ 0 ][ "group_name" ].as< std::string >();

	}


	uint64_t getGroupID( const Group& group )
	{
		constexpr pqxx::zview select_group_id_query { "SELECT group_id FROM groups WHERE group_name = $1" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result select_result { work->exec_params( select_group_id_query, group ) };

		if ( select_result.empty() )
		{
			spdlog::error( "No group with name {} found.", group );
			throw IDHANError( ErrorNo::DATABASE_DATA_NOT_FOUND, "No group with name " + group + " found." );
		}


		return select_result[ 0 ][ "group_id" ].as< uint64_t >();
	}


	void removeGroup( const Group& group )
	{
		constexpr pqxx::zview remove_query { "DELETE FROM groups WHERE group_name = $1" };

		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result remove_result { work->exec_params( remove_query, group ) };

		if ( remove_result.affected_rows() == 0 )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "0 rows effected while deleting " + group + " from groups."
			);
		}

	}


	void removeGroup( const uint64_t group_id )
	{
		constexpr pqxx::zview remove_query { "DELETE FROM groups WHERE group_id = $1" };

		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		const pqxx::result remove_result { work->exec_params( remove_query, group_id ) };

		if ( remove_result.affected_rows() == 0 )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "0 rows effected while deleting group_id: " +
				std::to_string( group_id ) +
				" from groups."
			);
		}
	}
}