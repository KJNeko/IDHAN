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

	namespace raw
	{
		void addMapping( pqxx::work& work, const uint64_t hash_id, const uint64_t tag ) try
		{
			constexpr pqxx::zview query { "INSERT INTO mappings ( hash_id, tag_id ) VALUES ( $1, $2 )" };


			work.exec_params( query, hash_id, tag );
		} catch ( pqxx::unique_violation& e )
		{

		}


		void deleteMapping( pqxx::work& work, const uint64_t hash_id, const uint64_t tag )
		{
			constexpr pqxx::zview query { "DELETE FROM mappings WHERE hash_id = $1 AND tag_id = $2" };


			const pqxx::result res { work.exec_params( query, hash_id, tag ) };
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


		std::vector< Tag > getMappings( pqxx::work& work, const uint64_t hash_id )
		{
			constexpr pqxx::zview query {
				"SELECT group_name, subtag, tag_id FROM tags NATURAL JOIN groups NATURAL JOIN subtags NATURAL JOIN mappings WHERE hash_id = $1;" };


			const pqxx::result res { work.exec_params( query, hash_id ) };

			std::vector< Tag > tags;

			for ( const auto& row: res )
			{
				tags.emplace_back(
					row[ "group_name" ].as< std::string >(), row[ "subtag" ].as< std::string >(), row[ "tag_id" ].as< uint64_t >()
				);
			}

			return tags;
		}


		std::vector< std::pair< uint64_t, Tag>>
		getMappingsGroup( pqxx::work& work, const std::vector< uint64_t >& hash_ids )
		{
			constexpr pqxx::zview query {
				"SELECT tag_id, count(tag_id) AS id_count FROM mappings where hash_id = any($1::bigint[]) group by tag_id order by tag_id" };


			const pqxx::result res { work.exec_params( query, hash_ids ) };

			std::vector< std::pair< uint64_t, Tag >> tags;

			std::vector< QFuture< Tag>> tags_futures;

			for ( const auto& row: res )
			{
				tags.emplace_back( row[ "id_count" ].as< uint64_t >(), tags::raw::getTag( work, row[ "tag_id" ].as< uint64_t >() ) );
			}

			return tags;
		}
	}

	namespace async
	{

		QFuture< void > addMapping( const uint64_t hash_id, const uint64_t tag_id )
		{


			static DatabasePipelineTemplate pipeline;
			Task< void, uint64_t, uint64_t > task { raw::addMapping, hash_id, tag_id };

			return pipeline.enqueue( task );
		}


		QFuture< void > deleteMapping( const uint64_t hash_id, const uint64_t tag_id )
		{


			static DatabasePipelineTemplate pipeline;
			Task< void, uint64_t, uint64_t > task { raw::deleteMapping, hash_id, tag_id };

			return pipeline.enqueue( task );
		}


		QFuture< std::vector< Tag >> getMappings( const uint64_t hash_id )
		{


			static DatabasePipelineTemplate pipeline;
			auto func = static_cast<std::vector< Tag >( * )( pqxx::work&, uint64_t )>(raw::getMappings);

			Task< std::vector< Tag >, uint64_t > task { func, hash_id };

			return pipeline.enqueue( task );
		}


		QFuture< std::vector< std::pair< uint64_t, Tag>> > getMappings( const std::vector< uint64_t >& hash_ids )
		{


			static DatabasePipelineTemplate pipeline;
			Task< std::vector< std::pair< uint64_t, Tag>>, std::vector< uint64_t>> task { raw::getMappingsGroup,
				hash_ids };

			return pipeline.enqueue( task );
		}
	}

}