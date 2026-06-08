//
// Created by kj16609 on 8/18/25.
//

#include "db/fixtures/ServerDBFixture.hpp"

#include <pqxx/connection>
#include <pqxx/nontransaction>

#include <memory>

#include "migrations.hpp"

void ServerDBFixture::SetUp()
{
	conn = std::make_unique< pqxx::connection >(
		"dbname=idhan-test "
		"user=idhan "
		"password=idhan "
		"host=localhost "
		"port=5432" );

	pqxx::nontransaction tx { *conn };

	// Drop extensions first so their objects aren't orphaned when schema is dropped
	tx.exec( "DROP EXTENSION IF EXISTS pg_trgm" );
	tx.exec( "DROP EXTENSION IF EXISTS pgcrypto" );
	tx.exec( "DROP SCHEMA IF EXISTS public CASCADE" );
	tx.exec( "CREATE SCHEMA public" );

	idhan::db::updateMigrations( tx, "public" );
}

void ServerDBFixture::TearDown()
{
	if ( conn )
	{
		conn->close();
		conn.reset();
	}
}
