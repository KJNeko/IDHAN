//
// Created by kj16609 on 7/8/22.
//

#include "tags.hpp"
#include "groups.hpp"
#include "subtags.hpp"

#include "databaseExceptions.hpp"

#include "TracyBox.hpp"


Tag getTag( const uint64_t tag_id )
{
	ZoneScoped;

	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT group_id, subtag_id FROM tags WHERE tag_id = $1" };

	const pqxx::result res { work.exec_params( query, tag_id ) };

	work.commit();

	if ( res.empty() )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No tag with tag_id " + std::to_string( tag_id ) + " found."
		);
	}

	const uint64_t group_id { res[ 0 ][ "group_id" ].as< uint64_t >() };
	const uint64_t subtag_id { res[ 0 ][ "subtag_id" ].as< uint64_t >() };

	return std::make_pair( getGroup( group_id ), getSubtag( subtag_id ) );
}


uint64_t getTagID( const std::string& group, const std::string& subtag, bool create )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	//Get id for group
	const uint64_t group_id { getGroupID( group, true ) };
	const uint64_t subtag_id { getSubtagID( subtag, true ) };

	constexpr pqxx::zview query { "SELECT tag_id FROM tags WHERE group_id = $1 AND subtag_id = $2" };
	const pqxx::result res { work.exec_params( query, group_id, subtag_id ) };

	if ( res.empty() && create )
	{
		constexpr pqxx::zview query_insert {
			"INSERT INTO tags ( group_id, subtag_id ) VALUES ( $1, $2 ) RETURNING tag_id" };
		const pqxx::result insert_res { work.exec_params( query_insert, group_id, subtag_id ) };
		work.commit();
		return insert_res[ 0 ][ "tag_id" ].as< uint64_t >();
	}
	else if ( res.empty() )
	{
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No tag found for " + group + ":" + subtag
		);
	}
	else
	{
		return res[ 0 ][ "tag_id" ].as< uint64_t >();
	}
}


void deleteTagID( const uint64_t tag_id )
{
	ZoneScoped;

	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "DELETE FROM tags WHERE tag_id = $1 CASCADE" };
	work.exec_params( query, tag_id );
	work.commit();
}


std::vector< Tag > getTags( const uint64_t hash_id )
{
	ZoneScoped;

	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query {
		"SELECT group_name, subtag FROM tags NATURAL JOIN groups NATURAL JOIN subtags NATURAL JOIN mappings WHERE hash_id = $1;" };

	const pqxx::result res { work.exec_params( query, hash_id ) };

	std::vector< Tag > tags;

	for ( const auto& row: res )
	{
		tags.emplace_back( row[ "group_name" ].as< std::string >(), row[ "subtag" ].as< std::string >() );
	}

	return tags;
}


