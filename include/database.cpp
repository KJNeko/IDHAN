
#include "database.hpp"

#include <QMimeDatabase>
#include <QMimeType>
#include <QSettings>

#include <iostream>

pqxx::work Database::getWork()
{
	return pqxx::work( conn );
}

void Database::init()
{
	// Ensure that all the tables we want exist

	auto work = getWork();

	work.exec( "create extension if not exists pg_trgm;" );

	work.exec( "CREATE TABLE IF NOT EXISTS files (hash_id BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE);" );

	work.exec( "CREATE TABLE IF NOT EXISTS subtags (subtag_id BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE);" );
	work.exec( "CREATE TABLE IF NOT EXISTS groups (group_id BIGSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE);" );

	work.exec( "CREATE TABLE IF NOT EXISTS tags (tag_id BIGSERIAL PRIMARY KEY, subtag_id BIGINT REFERENCES subtags(subtag_id), group_id BIGINT REFERENCES groups(group_id));" );

	work.exec( "CREATE TABLE IF NOT EXISTS mappings (hash_id BIGINT REFERENCES files(hash_id), tag_id BIGINT REFERENCES tags(tag_id));" );

	work.commit();
}
void Database::release()
{
	mtx.unlock();
}
Database::~Database()
{
	std::cout << "~Database()" << std::endl;
}
