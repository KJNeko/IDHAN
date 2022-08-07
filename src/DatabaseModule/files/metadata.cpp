//
// Created by kj16609 on 7/1/22.
//

#include "metadata.hpp"

#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"

#include <QMimeDatabase>


namespace metadata
{

	namespace raw
	{

		uint64_t getMimeID( pqxx::work& work, const std::string& mime )
		{
			ZoneScoped;
			constexpr pqxx::zview query { "SELECT mime_id FROM mime_types WHERE mime = $1" };

			const pqxx::result res { work.exec_params( query, mime ) };

			if ( res.empty() )
			{
				//This means the mime didn't exist in the database so we need to lock the table
				//and insert it.

				//Create the mime
				constexpr pqxx::zview query_insert { "INSERT INTO mime_types (mime) VALUES ($1) RETURNING mime_id" };

				return work.exec_params( query_insert, mime )[ 0 ][ "mime_id" ].as< uint64_t >();
			}

			return res[ 0 ][ "mime_id" ].as< uint64_t >();
		}


		void populateMime( pqxx::work& work, const uint64_t hash_id, const std::string& mime )
		{
			ZoneScoped;

			constexpr pqxx::zview query { "INSERT INTO mime (hash_id, mime_id) VALUES ($1, $2)" };

			const auto mime_id { raw::getMimeID( work, mime ) };

			const pqxx::result ret { work.exec_params( query, hash_id, mime_id ) };

		}


		std::string getMime( pqxx::work& work, const uint64_t hash_id )
		{
			ZoneScoped;

			constexpr pqxx::zview query { "SELECT mime FROM mime NATURAL JOIN mime_types WHERE hash_id = $1" };

			const pqxx::result ret { work.exec_params( query, hash_id ) };

			if ( ret.size() == 0 )
			{
				spdlog::error( "No mime found for hash_id {}", hash_id );
				throw IDHANError(
					ErrorNo::DATABASE_DATA_NOT_FOUND, "Failed to get mime for hash_id" + std::to_string( hash_id )
				);
			}

			return ret[ 0 ][ 0 ].as< std::string >();
		}

	}

	namespace async
	{
		QFuture< uint64_t > getMimeID( const std::string& mime )
		{
			static DatabasePipelineTemplate pipeline;
			Task< uint64_t, std::string > task { raw::getMimeID, mime };

			return pipeline.enqueue( task );
		}


		QFuture< void > populateMime( const uint64_t hash_id, const std::string& mime )
		{
			static DatabasePipelineTemplate pipeline;
			Task< void, uint64_t, std::string > task { raw::populateMime, hash_id, mime };

			return pipeline.enqueue( task );
		}


		QFuture< std::string > getMime( const uint64_t hash_id )
		{
			static DatabasePipelineTemplate pipeline;
			Task< std::string, uint64_t > task { raw::getMime, hash_id };

			return pipeline.enqueue( task );
		}
	}


	std::string getFileExtentionFromMime( const std::string mimeType )
	{
		const QMimeDatabase qtMimedb;

		const auto mime_type { qtMimedb.mimeTypeForName( QString::fromStdString( mimeType ) ) };

		return mime_type.preferredSuffix().toStdString();
	}


	std::string getFileExtention( const uint64_t hash_id )
	{
		ZoneScoped;

		const std::string mime { async::getMime( hash_id ).result() };

		return getFileExtentionFromMime( mime );
	}

}
