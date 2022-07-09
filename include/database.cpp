#include "database.hpp"

#include <QMimeType>

#include <iostream>

#include "TracyBox.hpp"


namespace Database
{
	void initalizeConnection( const std::string& connectionArgs )
	{
		ZoneScoped;
		spdlog::info( "Initalizing connections with settings '" + connectionArgs + "'" );

		ConnectionPool::init( connectionArgs );

		Connection conn;
		spdlog::info( "Connections made" );

		// Create the tables if they don't exist
		pqxx::work work { conn() };

		spdlog::info( "Checking tables" );

		work.exec( "create extension if not exists pg_trgm;" );

		work.exec( "CREATE TABLE IF NOT EXISTS files (hash_id BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE NOT NULL);" );

		work.exec( "CREATE TABLE IF NOT EXISTS subtags (subtag_id BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE NOT NULL);" );
		work.exec( "CREATE TABLE IF NOT EXISTS groups (group_id BIGSERIAL PRIMARY KEY, group_name TEXT UNIQUE NOT NULL);" );
		work.exec( "CREATE TABLE IF NOT EXISTS tags (tag_id BIGSERIAL PRIMARY KEY, subtag_id BIGINT REFERENCES subtags(subtag_id), group_id BIGINT REFERENCES groups(group_id) NOT NULL);" );

		work.exec( "CREATE TABLE IF NOT EXISTS mappings (hash_id BIGINT REFERENCES files(hash_id), tag_id BIGINT REFERENCES tags(tag_id) NOT NULL);" );

		work.exec( "CREATE TABLE IF NOT EXISTS deleted (hash_id BIGINT REFERENCES files(hash_id), delete_time TIMESTAMP NOT NULL);" );

		work.exec( "CREATE TABLE IF NOT EXISTS mime_types (mime_id BIGSERIAL PRIMARY KEY, mime TEXT UNIQUE NOT NULL);" );
		work.exec( "CREATE TABLE IF NOT EXISTS mime (hash_id BIGINT REFERENCES files(hash_id), mime_id BIGINT REFERENCES mime_types(mime_id));" );

		work.exec( "CREATE TABLE IF NOT EXISTS collections (collection_id BIGSERIAL PRIMARY KEY, collection_name TEXT UNIQUE NOT NULL);" );
		work.exec( "CREATE TABLE IF NOT EXISTS collections_filter (collection_id BIGINT REFERENCES collections(collection_id), tag_ids BIGINT[]);" );

		spdlog::info( "Tables checked" );


		work.commit();
	}
}