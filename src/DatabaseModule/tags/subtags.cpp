//
// Created by kj16609 on 6/28/22.
//

#include "subtags.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"

#include <QFuture>


namespace subtags
{
	namespace raw
	{
		uint64_t createSubtag( pqxx::work& work, const Subtag& subtag )
		{
			constexpr pqxx::zview checkSubtag { "SELECT subtag_id FROM subtags WHERE subtag = $1" };

			constexpr pqxx::zview query { "INSERT INTO subtags (subtag) VALUES ($1) RETURNING subtag_id" };


			ZoneScoped;


			//Check that it wasn't made before we locked
			const pqxx::result check_ret { work.exec_params( checkSubtag, subtag ) };
			if ( check_ret.size() )
			{
				return check_ret[ 0 ][ "subtag_id" ].as< uint64_t >();
			}


			const pqxx::result res { work.exec_params( query, subtag ) };

			work.commit();

			return res[ 0 ][ "subtag_id" ].as< uint64_t >();

		}


		Subtag getSubtag( pqxx::work& work, const uint64_t subtag_id )
		{
			constexpr pqxx::zview query { "SELECT subtag FROM subtags WHERE subtag_id = $1" };

			ZoneScoped;


			const pqxx::result res { work.exec_params( query, subtag_id ) };

			if ( res.empty() )
			{
				throw IDHANError(
					ErrorNo::DATABASE_DATA_NOT_FOUND, ( "No subtag with ID " + std::to_string( subtag_id ) + " found." )
				);
			}

			return res[ 0 ][ "subtag" ].as< std::string >();
		}


		uint64_t getSubtagID( pqxx::work& work, const Subtag& subtag )
		{
			constexpr pqxx::zview query { "SELECT subtag_id FROM subtags WHERE subtag = $1" };


			ZoneScoped;

			const pqxx::result res { work.exec_params( query, subtag ) };

			if ( res.empty() )
			{
				return 0;
			}
			
			return res[ 0 ][ "subtag_id" ].as< uint64_t >();
		}


		void deleteSubtag( pqxx::work& work, const uint64_t subtag_id )
		{
			ZoneScoped;


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