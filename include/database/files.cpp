//
// Created by kj16609 on 6/28/22.
//

#include "files.hpp"
#include "database.hpp"
#include "databaseExceptions.hpp"

#include <QByteArray>
#include <QString>

#include "TracyBox.hpp"

uint64_t addFile( const Hash& sha256, Database db )
{
	ZoneScoped;
	pqxx::work& work { db.getWork() };

	pqxx::params values;

	std::string query = "INSERT INTO files (sha256) VALUES ($1) RETURNING hash_id";
	std::basic_string_view<std::byte> sha256_view { sha256.data(), sha256.size() };
	values.append( sha256_view );

	const pqxx::result res = work.exec_params( query, values );
	work.commit();
	return res[ 0 ][ "hash_id" ].as<uint64_t>();
}

uint64_t getFileID( const Hash& sha256, const bool add, Database db )
{
	ZoneScoped;

	pqxx::result res;

	{
		ZoneScopedN( "getFileID_select" );
		pqxx::work& work { db.getWork() };

		pqxx::params values;

		std::string query = "SELECT hash_id FROM files WHERE sha256 = $1";
		std::basic_string_view<std::byte> sha256_view { sha256.data(), sha256.size() };
		values.append( sha256_view );

		res = work.exec_params( query, values );
		work.commit();
	}

	if ( res.empty() )
	{
		if ( add ) { return addFile( sha256, db ); }
		else
		{
			QByteArray hash_var = QByteArray::fromRawData(
				reinterpret_cast<const char*>( sha256.data() ),
				static_cast<qsizetype>( sha256.size() ) );
			QString hash_str = hash_var.toHex();
			throw EmptyReturn( "No file with hash " + hash_str.toStdString() + " found." );
		}
	}

	return res[ 0 ][ 0 ].as<uint64_t>();
}