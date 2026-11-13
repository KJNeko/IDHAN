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
	tx.exec( std::format(
		//language=postgresql
		R"SQL(
			DROP SCHEMA IF EXISTS {} CASCADE
		)SQL",
		tx.quote_name( schema ) ) );
	tx.exec(
		//language=postgresql
		R"SQL(
			CREATE SCHEMA
		)SQL"
		+ tx.quote_name( schema ) );
	tx.exec(
		//language=postgresql
		R"SQL(
			SET search_path =
		)SQL"
		+ db::makeSearchPath( schema ) );

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
		tx.exec( std::format(
			//language=postgresql
			R"SQL(
				DROP SCHEMA IF EXISTS {} CASCADE
			)SQL",
			tx.quote_name( m_schema ) ) );
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
		//language=postgresql
		R"SQL(
			TRUNCATE
			    subscription_groups,
			    downloader_parser_hosts,
			    download_sessions,
			    tags,
			    tag_namespaces,
			    tag_domains,
			    records,
			    file_info,
			    urls,
			    url_domains
			RESTART IDENTITY
			CASCADE
		)SQL" );
	tx.exec(
		//language=postgresql
		R"SQL(
			INSERT INTO tag_domains (domain_name)
			    VALUES ('default')
		)SQL" );
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
	return api().getWithKey( "/auth/verify", {} ).status == 200;
}

bool partitionExists( pqxx::connection& connection, const std::string_view prefix, const TagDomainID tag_domain_id )
{
	pqxx::nontransaction tx { connection };

	return tx.query_value< bool >(
		//language=postgresql
		R"SQL(
			SELECT
			    EXISTS (
			        SELECT
			            1
			        FROM pg_class c
			            JOIN pg_namespace n ON n.oid = c.relnamespace
			        WHERE n.nspname = current_schema()
			            AND c.relname = $1
			    )
		)SQL",
		pqxx::params { std::string( prefix ) + std::to_string( tag_domain_id ) } );
}

} // namespace idhan::test
