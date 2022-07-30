//
// Created by kj16609 on 7/1/22.
//

#include "metadata.hpp"

#include "database/utility/databaseExceptions.hpp"

#include "TracyBox.hpp"

#include <QMimeDatabase>


uint64_t getMimeID( const std::string& mime )
{
	ZoneScoped;

	const Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query { "SELECT mime_id FROM mime_types WHERE mime = $1" };

	const pqxx::result res { work->exec_params( query, mime ) };

	if ( res.empty() )
	{
		//This means the mime didn't exist in the database so we need to lock the table
		//and insert it.

		//Create the mime
		constexpr pqxx::zview query_insert { "INSERT INTO mime_types (mime) VALUES ($1) RETURNING mime_id" };

		return work->exec_params( query_insert, mime )[ 0 ][ "mime_id" ].as< uint64_t >();
	}

	return res[ 0 ][ "mime_id" ].as< uint64_t >();
}


void populateMime( const uint64_t hash_id, const std::string& mime )
{
	ZoneScoped;
	const Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query { "INSERT INTO MIME (hash_id, mime_id) VALUES ($1, $2)" };

	const auto mime_id { getMimeID( mime ) };

	const pqxx::result ret { work->exec_params( query, hash_id, mime_id ) };
}


std::string getMime( const uint64_t hash_id )
{
	ZoneScoped;
	const Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query { "SELECT mime FROM mime NATURAL JOIN mime_types WHERE hash_id = $1" };

	const pqxx::result ret { work->exec_params( query, hash_id ) };

	if ( ret.size() == 0 )
	{
		spdlog::error( "No mime found for hash_id {}", hash_id );
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "Failed to get mime for hash_id" + std::to_string( hash_id )
		);
	}

	return ret[ 0 ][ 0 ].as< std::string >();
}


std::string getFileExtention( const std::string mimeType )
{
	const QMimeDatabase qtMimedb;

	const auto mime_type { qtMimedb.mimeTypeForName( QString::fromStdString( mimeType ) ) };

	return mime_type.preferredSuffix().toStdString();
}


std::string getFileExtention( const uint64_t hash_id )
{
	ZoneScoped;

	const std::string mime { getMime( hash_id ) };

	return getFileExtention( mime );
}


