//
// Created by kj16609 on 8/10/26.
//

#include "helpers/testSchema.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/connection>
#include <pqxx/nontransaction>
#pragma GCC diagnostic pop

#include "db/searchPath.hpp"
#include "migrations.hpp"

std::string testConnectionString()
{
	std::string str {};
	str += "dbname=idhan-db ";
	str += "user=idhan ";
	str += "password=idhan ";
	str += "host=localhost ";
	str += "port=5432 ";
	str += "options='-c search_path=" + idhan::db::makeSearchPath( TEST_SCHEMA ) + "'";

	return str;
}

void resetTestSchema( pqxx::connection& conn )
{
	pqxx::nontransaction tx { conn };

	const auto quoted { tx.quote_name( TEST_SCHEMA ) };

	tx.exec( "DROP SCHEMA IF EXISTS " + quoted + " CASCADE" );
	tx.exec( "CREATE SCHEMA " + quoted );

	idhan::db::updateMigrations( tx, TEST_SCHEMA );
}
