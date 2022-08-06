#include "database.hpp"

#include <iostream>

#include "TracyBox.hpp"


namespace Database
{
	void initalizeConnection( const std::string& connectionArgs )
	{
		ZoneScoped;
		spdlog::info( "Initalizing connections with settings '" + connectionArgs + "'" );

		ConnectionPool::init( connectionArgs );

		const Connection conn;
		spdlog::info( "Connections made" );

		// Create the tables if they don't exist
		auto work { conn.getWork() };

		spdlog::info( "Checking tables" );

		constexpr pqxx::zview table_query { R"(
				CREATE TABLE IF NOT EXISTS files (hash_id BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE NOT NULL);
				CREATE TABLE IF NOT EXISTS subtags (subtag_id BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE NOT NULL);
				CREATE TABLE IF NOT EXISTS groups (group_id BIGSERIAL PRIMARY KEY, group_name TEXT UNIQUE NOT NULL);
				CREATE TABLE IF NOT EXISTS tags (tag_id BIGSERIAL PRIMARY KEY, subtag_id BIGINT REFERENCES subtags(subtag_id), group_id BIGINT REFERENCES groups(group_id) NOT NULL, UNIQUE(subtag_id, group_id));
				CREATE TABLE IF NOT EXISTS mappings (hash_id BIGINT REFERENCES files(hash_id), tag_id BIGINT REFERENCES tags(tag_id) NOT NULL, UNIQUE(hash_id, tag_id));
				CREATE TABLE IF NOT EXISTS mime_types (mime_id BIGSERIAL PRIMARY KEY, mime TEXT UNIQUE NOT NULL);
				CREATE TABLE IF NOT EXISTS mime (hash_id BIGINT REFERENCES files(hash_id), mime_id BIGINT REFERENCES mime_types(mime_id));
			)" };

		constexpr pqxx::zview index_query { R"(
				create extension if not exists pg_trgm;
				CREATE INDEX IF NOT EXISTS mappings_tag_index ON mappings(tag_id);
				CREATE INDEX IF NOT EXISTS mappings_hash_index ON mappings(hash_id);
				CREATE INDEX IF NOT EXISTS mime_mime_index ON mime(hash_id);
			)" };

		constexpr pqxx::zview view_query { R"(
				CREATE OR REPLACE VIEW concat_tags AS SELECT tag_id, concat(group_name, CASE WHEN length(group_name) = 0 THEN '' ELSE ':' END, subtag) AS joined_text FROM tags NATURAL JOIN subtags NATURAL JOIN groups;
			)" };

		work->exec( table_query );
		work->exec( index_query );
		work->exec( view_query );

		work->commit();
	}


	void RESETDATABASE()
	{
		Connection conn;
		auto work { conn.getWork() };

		work->exec( "DROP TABLE IF EXISTS files, subtags, groups, tags, mappings, deleted, mime_types, mime CASCADE" );

		ConnectionPool::deinit();
	}
}