//
// Created by kj16609 on 6/28/22.
//

#include "subtags.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"

#include <QFuture>
#include <QCache>


namespace subtags
{
	namespace raw
	{
		uint64_t createSubtag( pqxx::work& work, const Subtag& subtag )
		{
			constexpr pqxx::zview query { "INSERT INTO subtags (subtag) VALUES ($1) RETURNING subtag_id" };


			const uint64_t subtag_id { getSubtagID( work, subtag ) };
			if ( subtag_id != 0 )
			{
				return subtag_id;
			}

			const pqxx::result res { work.exec_params( query, subtag ) };

			work.commit();

			return res[ 0 ][ "subtag_id" ].as< uint64_t >();

		}


		Subtag getSubtag( pqxx::work& work, const uint64_t subtag_id )
		{
			constexpr pqxx::zview query { "SELECT subtag FROM subtags WHERE subtag_id = $1" };


			constexpr uint64_t BPerMB { 1000000 };
			constexpr uint64_t size { 64 * BPerMB };

			static QCache< uint64_t, Subtag > subtag_cache { size };
			if ( subtag_cache.contains( subtag_id ) )
			{
				return *subtag_cache.object( subtag_id );
			}

			const pqxx::result res { work.exec_params( query, subtag_id ) };

			if ( res.empty() )
			{
				throw IDHANError(
					ErrorNo::DATABASE_DATA_NOT_FOUND, ( "No subtag with ID " + std::to_string( subtag_id ) + " found." )
				);
			}

			const Subtag subtag { res[ 0 ][ "subtag" ].as< std::string >() };

			subtag_cache.insert( subtag_id, new Subtag( subtag ) );

			return subtag;
		}


		uint64_t getSubtagID( pqxx::work& work, const Subtag& subtag )
		{
			constexpr pqxx::zview query { "SELECT subtag_id FROM subtags WHERE subtag = $1" };


			constexpr uint64_t BPerMB { 1000000 };
			constexpr uint64_t size { 64 * BPerMB };

			static QCache< Subtag, uint64_t > subtag_cache { size };
			if ( subtag_cache.contains( subtag ) )
			{
				return *subtag_cache.object( subtag );
			}

			const pqxx::result res { work.exec_params( query, subtag ) };

			if ( res.empty() )
			{
				return 0;
			}

			const uint64_t subtag_id { res[ 0 ][ "subtag_id" ].as< uint64_t >() };

			subtag_cache.insert( subtag, new uint64_t( subtag_id ) );

			return subtag_id;
		}


		void deleteSubtag( pqxx::work& work, const uint64_t subtag_id )
		{


			constexpr pqxx::zview query { "DELETE FROM subtags WHERE subtag_id = $1 CASCADE" };

			const pqxx::result res { work.exec_params( query, subtag_id ) };

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

	namespace async
	{
		QFuture< uint64_t > createSubtag( const Subtag& subtag )
		{
			static DatabasePipelineTemplate pipeline;
			Task< uint64_t, Subtag > task { raw::createSubtag, subtag };

			return pipeline.enqueue( task );
		}


		QFuture< Subtag > getSubtag( const uint64_t subtag_id )
		{
			static DatabasePipelineTemplate pipeline;
			Task< Subtag, uint64_t > task { raw::getSubtag, subtag_id };

			return pipeline.enqueue( task );
		}


		QFuture< uint64_t > getSubtagID( const Subtag& subtag )
		{
			static DatabasePipelineTemplate pipeline;
			Task< uint64_t, Subtag > task { raw::getSubtagID, subtag };

			return pipeline.enqueue( task );
		}


		QFuture< void > deleteSubtag( const uint64_t subtag_id )
		{
			static DatabasePipelineTemplate pipeline;
			Task< void, uint64_t > task { raw::deleteSubtag, subtag_id };

			return pipeline.enqueue( task );
		}
	}
}