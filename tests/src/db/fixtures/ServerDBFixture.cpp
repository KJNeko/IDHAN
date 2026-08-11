//
// Created by kj16609 on 8/18/25.
//

#include "db/fixtures/ServerDBFixture.hpp"

#include <pqxx/connection>

#include <memory>

#include "helpers/testSchema.hpp"

void ServerDBFixture::SetUp()
{
	conn = std::make_unique< pqxx::connection >( testConnectionString() );
	resetTestSchema( *conn );
}

void ServerDBFixture::TearDown()
{
	if ( conn )
	{
		conn->close();
		conn.reset();
	}
}
