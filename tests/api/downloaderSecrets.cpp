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
			transaction.exec(
				//language=postgresql
				R"SQL(
					ALTER TABLE downloader_domain_secrets_unavailable RENAME TO downloader_domain_secrets
				)SQL" );
		}
		catch ( const std::exception& error )
		{
			WARN( "Failed to restore downloader_domain_secrets after failure test: " << error.what() );
		}
	}
};

SCENARIO_METHOD( ServerFixture, "Downloader secrets can be set and fetched through the API", "[api][downloader]" )
{
	Json::Value secrets {};
	secrets[ "example.com" ][ "apiKey" ] = "first";
	secrets[ "example.com" ][ "nested" ][ "enabled" ] = true;
	secrets[ "example.com" ][ "attempts" ] = 3;
	secrets[ "example.com" ][ "empty" ] = Json::Value {};
	secrets[ "other.example" ][ "userID" ] = "42";

	const auto created { api().post( "/downloader/secrets", secrets ) };
	REQUIRE( created.status == 200 );
	REQUIRE( created.json.isObject() );
	CHECK( created.json[ "example.com" ][ "nested" ][ "enabled" ].asBool() );
	CHECK( created.json[ "example.com" ][ "attempts" ].asInt() == 3 );
	CHECK( created.json[ "example.com" ][ "empty" ].isNull() );
	CHECK( created.json[ "example.com" ][ "apiKey" ].asString() == "first" );
	CHECK( created.json[ "other.example" ][ "userID" ].asString() == "42" );
	CHECK( created.header( "cache-control" ) == "no-store" );

	Json::Value update {};
	update[ "example.com" ][ "apiKey" ] = "second";
	const auto updated { api().post( "/downloader/secrets", update ) };
	REQUIRE( updated.status == 200 );
	CHECK( updated.json[ "example.com" ][ "apiKey" ].asString() == "second" );
	CHECK_FALSE( updated.json[ "example.com" ].isMember( "nested" ) );
	CHECK( updated.json[ "other.example" ][ "userID" ].asString() == "42" );

	const auto fetched { api().get( "/downloader/secrets" ) };
	REQUIRE( fetched.status == 200 );
	CHECK( fetched.json[ "example.com" ][ "apiKey" ].asString() == "second" );
	CHECK( fetched.json[ "other.example" ][ "userID" ].asString() == "42" );
	CHECK( fetched.header( "cache-control" ) == "no-store" );
}

SCENARIO_METHOD( ServerFixture, "Downloader secrets reject non-object domain values", "[api][downloader]" )
{
	Json::Value secrets {};
	secrets[ "example.com" ] = 123;

	const auto response { api().post( "/downloader/secrets", secrets ) };
	CHECK( response.status == 400 );
}

SCENARIO_METHOD( ServerFixture, "Downloader secrets reject invalid and duplicate domain scopes", "[api][downloader]" )
{
	for ( const auto* domain :
	      { "", "https://example.com", "example.com/path", "example.com:443", "user@example.com" } )
	{
		Json::Value secrets { Json::objectValue };
		secrets[ domain ] = Json::Value { Json::objectValue };
		CHECK( api().post( "/downloader/secrets", secrets ).status == 400 );
	}

	Json::Value duplicate { Json::objectValue };
	duplicate[ "EXAMPLE.com." ] = Json::Value { Json::objectValue };
	duplicate[ "example.com" ] = Json::Value { Json::objectValue };
	CHECK( api().post( "/downloader/secrets", duplicate ).status == 400 );
}

SCENARIO_METHOD(
	ServerFixture,
	"Downloader secret cache remains available after database write failures",
	"[api][downloader]" )
{
	Json::Value original { Json::objectValue };
	original[ "example.com" ][ "apiKey" ] = "cached-value";
	REQUIRE( api().post( "/downloader/secrets", original ).status == 200 );

	{
		pqxx::nontransaction transaction { db() };
		transaction.exec(
			//language=postgresql
			R"SQL(
				ALTER TABLE downloader_domain_secrets RENAME TO downloader_domain_secrets_unavailable
			)SQL" );
	}

	RestoreDownloaderSecretsTable restore { db() };

	const auto fetched { api().get( "/downloader/secrets" ) };
	REQUIRE( fetched.status == 200 );
	CHECK( fetched.json[ "example.com" ][ "apiKey" ].asString() == "cached-value" );

	Json::Value secrets {};
	secrets[ "example.com" ][ "apiKey" ] = "value";
	const auto updated { api().post( "/downloader/secrets", secrets ) };
	CHECK( updated.status == 500 );
	const auto unchanged { api().get( "/downloader/secrets" ) };
	REQUIRE( unchanged.status == 200 );
	CHECK( unchanged.json[ "example.com" ][ "apiKey" ].asString() == "cached-value" );
}

} // namespace idhan::test
