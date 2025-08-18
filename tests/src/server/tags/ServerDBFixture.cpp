//
// Created by kj16609 on 8/18/25.
//

#include "ServerDBFixture.hpp"

#include <pqxx/connection>
#include <pqxx/nontransaction>

#include <memory>

#include "migrations.hpp"

void ServerDBFixture::SetUp()
{
	conn = std::make_unique< pqxx::connection >(
		"dbname=idhan-test "
		"user=idhan "
		"host=localhost "
		"port=5432" );

	pqxx::nontransaction tx { *conn };

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
