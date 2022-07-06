#include "database.hpp"

#include <QMimeType>

#include <iostream>

#include "TracyBox.hpp"


Database::Database( Database& db )
{
	ZoneScoped;
	if ( conn == nullptr )
	{
		spdlog::critical( "Database connection not formed or invalid" );
		throw std::runtime_error( "Database connection not formed or invalid" );
	}

	txn = db.txn;
}


void Database::initalizeConnection( const std::string& connectionArgs )
{
	ZoneScoped;
	std::lock_guard< std::recursive_mutex > guard( txn_mtx );
	spdlog::info( "Initalizing connection with settings '" + connectionArgs + "'" );

	conn = new pqxx::connection( connectionArgs );

	if ( !conn->is_open() )
	{
		spdlog::critical( "Failed to open database connection" );
		throw std::runtime_error( "Failed to open database connection" );
	}


	// Create the tables if they don't exist
	pqxx::work work { *conn };

	work.exec( "create extension if not exists pg_trgm;" );

	work.exec( "CREATE TABLE IF NOT EXISTS files (hash_id BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS subtags (subtag_id BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE NOT NULL);" );
	work.exec( "CREATE TABLE IF NOT EXISTS groups (group_id BIGSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS tags (tag_id BIGSERIAL PRIMARY KEY, subtag_id BIGINT REFERENCES subtags(subtag_id), group_id BIGINT REFERENCES groups(group_id) NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS mappings (hash_id BIGINT REFERENCES files(hash_id), tag_id BIGINT REFERENCES tags(tag_id) NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS deleted (hash_id BIGINT REFERENCES files(hash_id), delete_time TIMESTAMP NOT NULL);" );

	work.exec( "CREATE TABLE IF NOT EXISTS mime_types (mime_id BIGSERIAL PRIMARY KEY, mime TEXT UNIQUE NOT NULL);" );
	work.exec( "CREATE TABLE IF NOT EXISTS mime (hash_id BIGINT REFERENCES files(hash_id), mime_id BIGINT REFERENCES mime_types(mime_id));" );

	work.commit();
}


std::shared_ptr< pqxx::work > Database::getWorkPtr()
{
	ZoneScoped;

	if ( *finalized )
	{
		spdlog::warn( "Database::getWorkPtr() called after finalization" );
		throw std::runtime_error( "Cannot get work pointer after commit() or abort()" );
	}

	if ( conn == nullptr )
	{
		spdlog::warn( "Database::getWorkPtr() called before connection initialized" );
		spdlog::critical( "Database connection not formed or invalid" );
		throw std::runtime_error( "Database connection not formed or invalid" );
	}
	else
	{
		if ( txn == nullptr )
		{
			txn = std::make_shared< pqxx::work >( *conn );
		}
	}

	return txn;
}


void Database::commit( bool throw_on_error )
{
	//Don't allow a commit unless txn is the only owner + 1
	if ( txn.use_count() > 2 )
	{
		if ( throw_on_error )
		{
			spdlog::warn(
				"Database::commit() called with txn.use_count() >= 2. txn.use_count == {}. If this is intentional call it with commit(false)", txn
				.use_count()
			);
		}
		else
		{
			return;
		}
	}
	else
	{
		if ( txn == nullptr )
		{
			return;
		}
		txn->commit();
		*finalized = true;
	}
}


void Database::abort( bool throw_on_error )
{
	//Don't allow an abort unless txn is the only owner + 1
	if ( txn.use_count() >= 2 )
	{
		if ( throw_on_error )
		{
			throw std::runtime_error( "Cannot abort unless txn is the only owner" );
		}
		else
		{
			return;
		}
	}
	else
	{
		txn->abort();
		*finalized = true;
	}
}


Database::~Database()
{
	if ( ( !( *finalized ) && !( txn.use_count() >= 2 || !( txn.use_count() ) ) ) )
	{
		spdlog::warn( "~Database() called without commit() or abort(), finalized: {}, use_count: {}", *finalized, txn.use_count() );
		throw std::runtime_error( "~Database() called without commit() or abort()" );
	}
}