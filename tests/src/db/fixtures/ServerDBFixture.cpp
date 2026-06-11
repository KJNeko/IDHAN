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
		"dbname=idhan-db "
		"user=idhan "
		"password=idhan "
		"host=localhost "
		"port=5432 "
		"options='-c search_path=test,public'" );

	pqxx::nontransaction tx { *conn };

	// Drop extensions first so their objects aren't orphaned when schema is dropped
	tx.exec( "DROP SCHEMA IF EXISTS test CASCADE" );
	tx.exec( "CREATE SCHEMA test" );

	idhan::db::updateMigrations( tx, "test" );
}

void ServerDBFixture::TearDown()
{
	if ( conn )
	{
		conn->close();
		conn.reset();
	}
}
