//
// Created by kj16609 on 6/28/22.
//

#include "files.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"


#include <QByteArray>
#include <QString>

#include <iostream>

#include "TracyBox.hpp"

uint64_t addFile( const Hash& sha256 )
{
	ZoneScoped;
	Database db;
	pqxx::work work { db.getWork() };

	pqxx::params values;

	std::string query = "INSERT INTO files (sha256) VALUES ($1) RETURNING hash_id";
	std::basic_string_view<std::byte> sha256_view { sha256.data(), sha256.size() };
	values.append( sha256_view );

	const pqxx::result res = work.exec_params( query, values );
	work.commit();
	return res[ 0 ][ "hash_id" ].as<uint64_t>();
}

uint64_t getFileID( const Hash& sha256, const bool add = false )
{
	ZoneScoped;

	pqxx::result res;

	{
		Database db;
		pqxx::work work { db.getWork() };

		// Hash to hex conversion via QString
		// TODO: Figure out a better way or just start using exec_prepared again

		pqxx::params values;

		std::string query = "SELECT hash_id FROM files WHERE sha256 = $1";
		std::basic_string_view<std::byte> sha256_view { sha256.data(), sha256.size() };
		values.append( sha256_view );

		res = work.exec_params( query, values );
		work.commit();
	}

	if ( res.empty() )
	{
		if ( add )
		{

			return addFile( sha256 );
		}
		else
		{
			QByteArray hash_var = QByteArray::fromRawData( (const char*)sha256.data(), sha256.size() );
			QString hash_str	= hash_var.toHex();
			throw EmptyReturn( "No file with hash " + hash_str.toStdString() + " found." );
		}
	}

	return res[ 0 ][ 0 ].as<uint64_t>();
}