//
// Created by kj16609 on 6/28/22.
//

#include "mappings.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/utility/databaseExceptions.hpp"
#include "tags.hpp"

#include "TracyBox.hpp"


namespace mappings
{
	void addMapping( const uint64_t hash_id, const std::string& group, const std::string& subtag )
	{
		constexpr pqxx::zview query { "INSERT INTO mappings ( hash_id, tag_id ) VALUES ( $1, $2 )" };

		ZoneScoped;
		const uint64_t tag_id { tags::getTagID( group, subtag, true ) };

		const Connection conn;
		auto work { conn.getWork() };


		work->exec_params( query, hash_id, tag_id );
	}


	void addMapping( const uint64_t hash_id, const uint64_t tag )
	{
		constexpr pqxx::zview query { "INSERT INTO mappings ( hash_id, tag_id ) VALUES ( $1, $2 )" };

		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		work->exec_params( query, hash_id, tag );
	}


	void deleteMapping( const uint64_t hash_id, const std::string& group, const std::string& subtag )
	{
		constexpr pqxx::zview query { "DELETE FROM mappings WHERE hash_id = $1 AND tag_id = $1" };


		ZoneScoped;
		const uint64_t tag_id { tags::getTagID( group, subtag, true ) };

		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result res { work->exec_params( query, hash_id, tag_id ) };

		if ( res.affected_rows() == 0 )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "No mapping found for " +
				std::to_string( hash_id ) +
				" in " +
				group +
				":" +
				subtag
			);
		}
	}


	void deleteMapping( const uint64_t hash_id, const uint64_t tag )
	{
		constexpr pqxx::zview query { "DELETE FROM mappings WHERE hash_id = $1 AND tag_id = $2" };


		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };
		const pqxx::result res { work->exec_params( query, hash_id, tag ) };
		if ( res.affected_rows() == 0 )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "No mapping found for " +
				std::to_string( hash_id ) +
				" in tag_id: " +
				std::to_string( tag )
			);
		}
	}


	void addMappingToHash( const Hash32& sha256, const std::string& group, const std::string& subtag )
	{
		constexpr pqxx::zview checkMapping { "SELECT * FROM mappings WHERE tag_id = $1 AND hash_id = $2" };


		ZoneScoped;

		const Connection conn;
		auto work { conn.getWork() };

		const uint64_t tag_id { tags::getTagID( group, subtag, true ) };
		const uint64_t hash_id { files::getFileID( sha256, true ) };

		//Check that it wasn't made before we locked
		const pqxx::result check_ret { work->exec_params( checkMapping, tag_id, hash_id ) };
		if ( check_ret.size() )
		{
			return;
		}

		addMapping( hash_id, group, subtag );
	}


	void removeMappingFromHash( const Hash32& sha256, const std::string& group, const std::string& subtag )
	{
		const uint64_t hash_id { files::getFileID( sha256, false ) };

		deleteMapping( hash_id, group, subtag );
	}


	std::vector< Tag > getMappings( const uint64_t hash_id )
	{
		constexpr pqxx::zview query {
			"SELECT group_name, subtag, tag_id FROM tags NATURAL JOIN groups NATURAL JOIN subtags NATURAL JOIN mappings WHERE hash_id = $1;" };

		ZoneScoped;

		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result res { work->exec_params( query, hash_id ) };

		std::vector< Tag > tags;

		for ( const auto& row: res )
		{
			tags.emplace_back(
				row[ "group_name" ].as< std::string >(), row[ "subtag" ].as< std::string >(), row[ "tag_id" ].as< uint64_t >()
			);
		}

		return tags;
	}


	std::vector< std::pair< uint64_t, Tag>> getMappings( const std::vector< uint64_t >& hash_ids )
	{
		constexpr pqxx::zview query {
			"SELECT tag_id, count(tag_id) AS id_count FROM mappings where hash_id = any($1::bigint[]) group by tag_id order by tag_id" };

		ZoneScoped;

		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result res { work->exec_params( query, hash_ids ) };

		std::vector< std::pair< uint64_t, Tag >> tags;

		for ( const auto& row: res )
		{
			tags.emplace_back( row[ "id_count" ].as< uint64_t >(), tags::getTag( row[ "tag_id" ].as< uint64_t >() ) );
		}

		return tags;
	}
}