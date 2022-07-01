
#include "database.hpp"

#include <QMimeDatabase>
#include <QMimeType>
#include <QSettings>

#include <iostream>

#include "TracyBox.hpp"

Database::Database()
{
	std::lock_guard<std::mutex> conGuard( conMtx );
	ZoneScoped;
	if ( conn == nullptr )
	{
		spdlog::critical( "Database connection not formed or invalid" );
		throw std::runtime_error( "Database connection not formed or invalid" );
	}
	else
	{
		guard = std::make_shared<std::lock_guard<std::mutex>>( mtx );
		txn	  = std::make_shared<pqxx::work>( *conn );
	}
}

Database::Database( Database& db )
{
	ZoneScoped;
	if ( conn == nullptr )
	{
		spdlog::critical( "Database connection not formed or invalid" );
		throw std::runtime_error( "Database connection not formed or invalid" );
	}

	// Copy the txn and guard
	guard = db.guard;
	txn	  = db.txn;
}

void Database::initalizeConnection( const std::string& connectionArgs )
{
	ZoneScoped;
	std::lock_guard<std::mutex> guard( mtx );
	spdlog::info( "Initalizing connection with settings '" + connectionArgs + "'" );

	conn = new pqxx::connection( connectionArgs );

	if ( !conn->is_open() )
	{
		spdlog::critical( "Failed to open database connection" );
		throw std::runtime_error( "Failed to open database connection" );
	}

	spdlog::info( "Connection opened" );

	// Create the tables if they don't exist
	pqxx::work work { *conn };

	work.exec( "create extension if not exists pg_trgm;" );

	work.exec( "CREATE TABLE IF NOT EXISTS files (hash_id BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS subtags (subtag_id BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE NOT NULL);" );
	work.exec( "CREATE TABLE IF NOT EXISTS groups (group_id BIGSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS tags (tag_id BIGSERIAL PRIMARY KEY, subtag_id BIGINT REFERENCES subtags(subtag_id), group_id BIGINT REFERENCES groups(group_id) NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS mappings (hash_id BIGINT REFERENCES files(hash_id), tag_id BIGINT REFERENCES tags(tag_id) NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS deleted (hash_id BIGINT REFERENCES files(hash_id), delete_time TIMESTAMP NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS mime (hash_id BIGINT REFERENCES files(hash_id), mime TEXT NOT NULL);" );

	work.commit();
}

pqxx::work& Database::getWork()
{
	ZoneScoped;
	return *txn;
}
