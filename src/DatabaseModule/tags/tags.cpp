//
// Created by kj16609 on 7/8/22.
//

#include <QCache>

#include "tags.hpp"
#include "groups.hpp"
#include "subtags.hpp"

#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"


namespace tags
{
	namespace internal
	{
		std::pair< uint64_t, uint64_t > getGroupAndSubtagIDs( const Group& group, const Subtag& subtag )
		{
			ZoneScoped;
			//Get id for group
			const uint64_t group_id { groups::getGroupID( group ) };
			const uint64_t subtag_id { subtags::getSubtagID( subtag ) };

			return { group_id, subtag_id };
		}
	}


	Tag getTag( const uint64_t tag_id )
	{
		constexpr pqxx::zview query { "SELECT group_id, subtag_id FROM tags WHERE tag_id = $1" };

		ZoneScoped;
		static QCache< uint64_t, Tag > tag_cache { 5000 };

		if ( tag_cache.contains( tag_id ) )
		{
			return *tag_cache.object( tag_id );
		}

		const Connection conn;
		auto work { conn.getWork() };


		const pqxx::result res { work->exec_params( query, tag_id ) };

		if ( res.empty() )
		{
			throw IDHANError(
				ErrorNo::DATABASE_DATA_NOT_FOUND, "No tag with tag_id " + std::to_string( tag_id ) + " found."
			);
		}

		const uint64_t group_id { res[ 0 ][ "group_id" ].as< uint64_t >() };
		const uint64_t subtag_id { res[ 0 ][ "subtag_id" ].as< uint64_t >() };

		return { groups::getGroup( group_id ), subtags::getSubtag( subtag_id ), tag_id };
	}


	uint64_t createTag( const Group& group, const Subtag& subtag )
	{
		constexpr pqxx::zview query_insert {
			"INSERT INTO tags ( group_id, subtag_id ) VALUES ( $1, $2 ) RETURNING tag_id" };

		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		const auto [ group_id, subtag_id ] = [ & ]() -> std::pair< uint64_t, uint64_t >
		{
			uint64_t group_id_ { 0 };
			uint64_t subtag_id_ { 0 };
			try
			{
				group_id_ = groups::getGroupID( group );
			}
			catch ( const IDHANError& e )
			{
				group_id_ = groups::createGroup( group );
			}

			try
			{
				subtag_id_ = subtags::getSubtagID( group );
			}
			catch ( const IDHANError& e )
			{
				subtag_id_ = subtags::createSubtag( subtag );
			}

			return { group_id_, subtag_id_ };
		}();

		const pqxx::result insert_res { work->exec_params( query_insert, group_id, subtag_id ) };
		return insert_res[ 0 ][ "tag_id" ].as< uint64_t >();
	}


	uint64_t getTagID( const Group& group, const Subtag& subtag, bool create )
	{
		constexpr pqxx::zview query { "SELECT tag_id FROM tags WHERE group_id = $1 AND subtag_id = $2" };

		ZoneScoped;
		const Connection conn;
		auto work { conn.getWork() };

		const auto [ group_id, subtag_id ] { internal::getGroupAndSubtagIDs( group, subtag ) };

		const pqxx::result res { work->exec_params( query, group_id, subtag_id ) };

		if ( res.empty() && create )
		{
			return createTag( group, subtag );
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


	void deleteTagFromID( const uint64_t tag_id )
	{
		constexpr pqxx::zview query { "DELETE FROM tags WHERE tag_id = $1" };

		constexpr pqxx::zview query_clean_subtags {
			"DELETE FROM subtags WHERE subtag_id NOT IN (SELECT subtag_id FROM tags)" };
		constexpr pqxx::zview query_clean_groups {
			"DELETE FROM groups WHERE group_id NOT IN (SELECT group_id FROM tags)" };


		ZoneScoped;

		const Connection conn;
		auto work { conn.getWork() };

		work->exec_params( query, tag_id );

		work->exec( query_clean_subtags );
		work->exec( query_clean_groups );
	}


}



