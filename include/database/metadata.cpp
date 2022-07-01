//
// Created by kj16609 on 7/1/22.
//

#include "metadata.hpp"

#include "databaseExceptions.hpp"

#include "TracyBox.hpp"

#include <QMimeDatabase>

void populateMime( const uint64_t hash_id, const std::string& mime, Database db )
{
	ZoneScoped;

	pqxx::work& work { db.getWork() };

	std::string query = "INSERT INTO mime (hash_id, mime) VALUES (" +
						std::to_string( hash_id ) + ", '" + mime + "')";

	pqxx::result ret = work.exec_params( query );

	if ( ret.affected_rows() == 0 )
	{
		throw EmptyReturnException( "Failed to insert mime" );
	}

	work.commit();

	return;
}

std::string getMime( const uint64_t hash_id, Database db )
{
	ZoneScoped;

	pqxx::work& work { db.getWork() };

	std::string query =
		"SELECT mime FROM mime WHERE hash_id = " + std::to_string( hash_id );

	pqxx::result ret = work.exec_params( query );

	if ( ret.size() == 0 )
	{
		spdlog::error( "No mime found for hash_id {}", hash_id );
		throw EmptyReturnException( "Failed to get mime" );
	}

	return ret[ 0 ][ 0 ].as<std::string>();
}

std::string getFileExtention(const std::string mimeType)
{
	QMimeDatabase qtMimedb;

	auto mime_type = qtMimedb.mimeTypeForName( QString::fromStdString( mimeType ) );

	return mime_type.preferredSuffix().toStdString();
}

std::string getFileExtention( const uint64_t hash_id, Database db )
{
	ZoneScoped;

	const std::string mime = getMime( hash_id, db );

	return getFileExtention(mime);
}


