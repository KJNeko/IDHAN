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

	namespace raw
	{

		Tag getTag( pqxx::work& work, const uint64_t tag_id )
		{
			constexpr pqxx::zview query { "SELECT group_id, subtag_id FROM tags WHERE tag_id = $1" };

			ZoneScoped;
			static QCache< uint64_t, Tag > tag_cache { 5000 };

			if ( tag_cache.contains( tag_id ) )
			{
				return *tag_cache.object( tag_id );
			}


			const pqxx::result res { work.exec_params( query, tag_id ) };

			if ( res.empty() )
			{
				throw IDHANError(
					ErrorNo::DATABASE_DATA_NOT_FOUND, "No tag with tag_id " + std::to_string( tag_id ) + " found."
				);
			}

			const uint64_t group_id { res[ 0 ][ "group_id" ].as< uint64_t >() };
			const uint64_t subtag_id { res[ 0 ][ "subtag_id" ].as< uint64_t >() };

			const QFuture< Group > group { groups::async::getGroup( group_id ) };
			const QFuture< Subtag > subtag { subtags::async::getSubtag( subtag_id ) };

			return { group.result(), subtag.result(), tag_id };
		}


		uint64_t createTag( pqxx::work& work, const Group& group, const Subtag& subtag )
		{

			constexpr pqxx::zview query_insert {
				"INSERT INTO tags ( group_id, subtag_id ) VALUES ( $1, $2 ) RETURNING tag_id" };


			ZoneScoped;


			const auto [ group_id, subtag_id ] = [ & ]() -> std::pair< uint64_t, uint64_t >
			{
				QFuture< uint64_t > group_id_;
				QFuture< uint64_t > subtag_id_;

				group_id_ = groups::async::getGroupID( group );
				subtag_id_ = subtags::async::getSubtagID( subtag );

				if ( group_id_.result() == 0 )
				{
					group_id_ = groups::async::createGroup( group );
				}

				if ( subtag_id_.result() == 0 )
				{
					subtag_id_ = subtags::async::createSubtag( subtag );
				}

				spdlog::info( "Tag {}:{} created with ids {}:{}", group, subtag, group_id_.result(), subtag_id_.result() );

				return { group_id_.result(), subtag_id_.result() };
			}();

			//Check if it exists
			const auto tag_id { getTagID( work, group, subtag ) };
			if ( tag_id == 0 )
			{
				spdlog::info( "Tag {}:{} does not exist. Creating", group, subtag );
				const pqxx::result res { work.exec_params( query_insert, group_id, subtag_id ) };
				work.commit();
				return res[ 0 ][ "tag_id" ].as< uint64_t >();
			}
			else
			{
				spdlog::info( "Tag {}:{} already existed", group, subtag );
				return tag_id;
			};
		}


		uint64_t getTagID( pqxx::work& work, const Group& group, const Subtag& subtag )
		{
			constexpr pqxx::zview query { "SELECT tag_id FROM tags WHERE group_id = $1 AND subtag_id = $2" };

			ZoneScoped;

			const auto [ group_id, subtag_id ] = [ & ]() -> std::pair< uint64_t, uint64_t >
			{
				QFuture< uint64_t > group_id_;
				QFuture< uint64_t > subtag_id_;

				group_id_ = groups::async::getGroupID( group );
				subtag_id_ = subtags::async::getSubtagID( subtag );

				if ( group_id_.result() == 0 || subtag_id_.result() == 0 )
				{
					spdlog::info( "Tag {}:{} does not exist... {}:{}", group, subtag, group_id_.result(), subtag_id_.result() );
					return { 0, 0 };
				}

				return { group_id_.result(), subtag_id_.result() };
			}();

			if ( group_id == 0 || subtag_id == 0 )
			{
				return 0;
			}

			const pqxx::result res { work.exec_params( query, group_id, subtag_id ) };

			if ( res.empty() )
			{
				return 0;
			}
			else
			{
				return res[ 0 ][ "tag_id" ].as< uint64_t >();
			}
		}


		void deleteTagFromID( pqxx::work& work, const uint64_t tag_id )
		{
			constexpr pqxx::zview query { "DELETE FROM tags WHERE tag_id = $1" };

			constexpr pqxx::zview query_clean_subtags {
				"DELETE FROM subtags WHERE subtag_id NOT IN (SELECT subtag_id FROM tags)" };
			constexpr pqxx::zview query_clean_groups {
				"DELETE FROM groups WHERE group_id NOT IN (SELECT group_id FROM tags)" };


			ZoneScoped;

			work.exec_params( query, tag_id );

			work.exec( query_clean_subtags );
			work.exec( query_clean_groups );
		}
	}

	namespace async
	{
		QFuture< Tag > getTag( const uint64_t tag_id )
		{
			static DatabasePipelineTemplate pipeline;
			Task< Tag, uint64_t > task { raw::getTag, tag_id };

			return pipeline.enqueue( task );
		}


		QFuture< uint64_t > createTag( const Group& group, const Subtag& subtag )
		{
			static DatabasePipelineTemplate pipeline;
			Task< uint64_t, Group, Subtag > task { raw::createTag, group, subtag };

			return pipeline.enqueue( task );
		}


		QFuture< uint64_t > getTagID( const Group& group, const Subtag& subtag )
		{
			static DatabasePipelineTemplate pipeline;
			Task< uint64_t, Group, Subtag > task { raw::getTagID, group, subtag };

			return pipeline.enqueue( task );
		}


		QFuture< void > deleteTagFromID( const uint64_t tag_id )
		{
			static DatabasePipelineTemplate pipeline;
			Task< void, uint64_t > task { raw::deleteTagFromID, tag_id };

			return pipeline.enqueue( task );
		}
	}


}



