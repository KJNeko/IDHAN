#include "ServerFixture.hpp"

#include <unistd.h>

#include <format>

#include <catch2/catch_test_macros.hpp>

#include "../../common/TestConnection.hpp"
#include "db/searchPath.hpp"

namespace idhan::test
{

//! Empties the schema the server is about to migrate, so a run never inherits the last one's rows, and
//! points this connection at it.
static std::string resetSchema( pqxx::connection& connection, std::string schema )
{
	pqxx::nontransaction tx { connection };
	tx.exec( "DROP SCHEMA IF EXISTS " + tx.quote_name( schema ) + " CASCADE" );
	tx.exec( "CREATE SCHEMA " + tx.quote_name( schema ) );
	tx.exec( "SET search_path = " + db::makeSearchPath( schema ) );

	return schema;
}

//! One schema per test process, so ctest can run the cases in parallel without them treading on each other.
static std::string schemaName()
{
	return envOr( "IDHAN_TEST_API_SCHEMA", std::format( "test_api_{}", ::getpid() ) );
}

TestServer::TestServer() :
  m_connection( connectionString() ),
  m_schema( resetSchema( m_connection, schemaName() ) ),
  m_server( m_schema ),
  m_client( m_server.port() )
{
	m_client.authenticate();
}

TestServer::~TestServer()
{
	// the schema cannot be dropped while the server still holds connections to it
	m_server.stop();

	try
	{
		pqxx::nontransaction tx { m_connection };
		tx.exec( "DROP SCHEMA IF EXISTS " + tx.quote_name( m_schema ) + " CASCADE" );
	}
	catch ( const std::exception& e )
	{
		WARN( "Failed to drop the API test schema " << m_schema << ": " << e.what() );
	}
}

void TestServer::wipe()
{
	pqxx::nontransaction tx { m_connection };
	tx.exec(
		"TRUNCATE tags, tag_namespaces, tag_domains, records, file_info, urls, url_domains "
		"RESTART IDENTITY CASCADE" );
	tx.exec( "INSERT INTO tag_domains (domain_name) VALUES ('default')" );
}

TestServer& testServer()
{
	static TestServer server {};
	return server;
}

ServerFixture::~ServerFixture()
{
	try
	{
		testServer().wipe();
	}
	catch ( const std::exception& e )
	{
		WARN( "Failed to wipe the tag tables between tests: " << e.what() );
	}
}

bool ServerFixture::authDisabled() const
{
	return api().getWithKey( "/auth/verify", {} ).status == drogon::k200OK;
}

bool partitionExists( pqxx::connection& connection, const std::string_view prefix, const TagDomainID tag_domain_id )
{
	pqxx::nontransaction tx { connection };

	return tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace "
		"WHERE n.nspname = current_schema() AND c.relname = $1)",
		pqxx::params { std::string( prefix ) + std::to_string( tag_domain_id ) } );
}

} // namespace idhan::test
