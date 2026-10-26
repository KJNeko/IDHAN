#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{
class RestoreDownloaderSecretsTable
{
	pqxx::connection& m_db;

  public:

	explicit RestoreDownloaderSecretsTable( pqxx::connection& db ) : m_db( db ) {}

	~RestoreDownloaderSecretsTable() noexcept
	{
		try
		{
			pqxx::nontransaction transaction { m_db };
			transaction.exec( "ALTER TABLE downloader_secrets_unavailable RENAME TO downloader_secrets" );
		}
		catch ( const std::exception& error )
		{
			WARN( "Failed to restore downloader_secrets after failure test: " << error.what() );
		}
	}
};

SCENARIO_METHOD( ServerFixture, "Downloader secrets can be set and fetched through the API", "[api][downloader]" )
{
	Json::Value secrets {};
	secrets[ "example.apiKey" ] = "first";
	secrets[ "example.userID" ] = "42";

	const auto created { api().post( "/downloader/secrets", secrets ) };
	REQUIRE( created.status == 200 );
	REQUIRE( created.json.isObject() );
	CHECK( created.json[ "example.apiKey" ].asString() == "first" );
	CHECK( created.json[ "example.userID" ].asString() == "42" );
	CHECK( created.header( "cache-control" ) == "no-store" );

	Json::Value update {};
	update[ "example.apiKey" ] = "second";
	const auto updated { api().post( "/downloader/secrets", update ) };
	REQUIRE( updated.status == 200 );
	CHECK( updated.json[ "example.apiKey" ].asString() == "second" );
	CHECK( updated.json[ "example.userID" ].asString() == "42" );

	const auto fetched { api().get( "/downloader/secrets" ) };
	REQUIRE( fetched.status == 200 );
	CHECK( fetched.json[ "example.apiKey" ].asString() == "second" );
	CHECK( fetched.json[ "example.userID" ].asString() == "42" );
	CHECK( fetched.header( "cache-control" ) == "no-store" );
}

SCENARIO_METHOD( ServerFixture, "Downloader secrets reject non-string values", "[api][downloader]" )
{
	Json::Value secrets {};
	secrets[ "example.apiKey" ] = 123;

	const auto response { api().post( "/downloader/secrets", secrets ) };
	CHECK( response.status == 400 );
}

SCENARIO_METHOD( ServerFixture, "Downloader secret database failures are server errors", "[api][downloader]" )
{
	{
		pqxx::nontransaction transaction { db() };
		transaction.exec( "ALTER TABLE downloader_secrets RENAME TO downloader_secrets_unavailable" );
	}

	RestoreDownloaderSecretsTable restore { db() };

	const auto fetched { api().get( "/downloader/secrets" ) };
	CHECK( fetched.status == 500 );

	Json::Value secrets {};
	secrets[ "example.apiKey" ] = "value";
	const auto updated { api().post( "/downloader/secrets", secrets ) };
	CHECK( updated.status == 500 );
}

} // namespace idhan::test
