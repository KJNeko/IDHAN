#include <catch2/catch_test_macros.hpp>

#include "IDHANDownloader/secrets/SecretDomain.hpp"
#include "harness/Fixtures.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/ScriptSession.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::test
{

class SecretTestSession final : public downloader::ScriptSession
{
  public:

	MapSecrets provider;
	bool fail {};

	std::expected< Json::Value, std::string > secrets( const std::string_view domain ) override
	{
		if ( fail ) return std::unexpected( "Secret store unavailable" );

		return provider.secrets( domain );
	}

	downloader::FollowResult follow( downloader::ScriptExecution&, std::string ) override { return {}; }

	std::expected< void, std::string > setUrlType( std::string, std::string ) override { return {}; }

	void startRequest( downloader::ScriptExecution&, Json::Value, downloader::PendingPromise ) override {}

	std::shared_ptr< downloader::ImportStatus > startImport( downloader::ScriptExecution&, Json::Value ) override
	{
		return std::make_shared< downloader::ImportStatus >();
	}
};

Json::Value evaluateSecrets(
	downloader::ScriptRegistry& registry,
	SecretTestSession& session,
	const std::string& url,
	const std::string& source )
{
	const auto route_e { registry.route( url ) };
	REQUIRE( route_e.has_value() );
	REQUIRE( route_e.value().has_value() );
	downloader::ScriptContext scripts { {}, registry.bytecode() };
	downloader::ScriptExecution execution;
	execution.session = &session;
	execution.parser_url = url;
	execution.route = route_e.value().value();
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();
	downloader::JSValueHandle result {
		realm.get(), JS_Eval( realm.get(), source.c_str(), source.size(), "domain-secrets.js", JS_EVAL_TYPE_GLOBAL )
	};
	const bool failed { JS_IsException( result.get() ) != 0 };
	INFO( ( failed ? downloader::scriptExceptionString( realm.get() ) : "" ) );
	REQUIRE_FALSE( failed );
	const auto json_e { downloader::scriptValueToJson( realm.get(), result.get() ) };
	REQUIRE( json_e.has_value() );
	return json_e.value();
}

TEST_CASE( "Secret objects stay within the configured domain and its subdomains", "[downloader][secrets]" )
{
	ParserFixture files;
	files.write( "parser.js", "export function post() {}" );
	files.write( "url-classes.json", R"JSON({"urlClasses":[{
    "name":"example", "script":"parser.js", "hosts":["example.co.uk","mirror.test"],
    "routes":[{"export":"post"}]
}]})JSON" );
	auto registry_e { downloader::ScriptRegistry::create( files.urlClassesPath(), files.root() ) };
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	SecretTestSession session;
	Json::Value document { Json::objectValue };
	document[ "credentials" ][ "token" ] = "test-token";
	document[ "enabled" ] = true;
	document[ "limit" ] = 12;
	document[ "nested" ] = Json::Value { Json::arrayValue };
	document[ "nested" ].append( Json::Value {} );
	document[ "nested" ].append( 3 );
	session.provider.values.emplace( "example.co.uk", document );
	Json::Value mirror { Json::objectValue };
	mirror[ "credentials" ][ "token" ] = "different-token";
	session.provider.values.emplace( "mirror.test", mirror );

	for ( const auto* hostname : { "example.co.uk", "subdomain.example.co.uk", "a.b.example.co.uk" } )
	{
		CHECK(
			evaluateSecrets( registry, session, "https://" + std::string { hostname } + "/post", "idhan.secrets()" )
		    == document );
	}

	CHECK( evaluateSecrets( registry, session, "https://mirror.test/post", "idhan.secrets()" ) == mirror );
	CHECK( evaluateSecrets( registry, session, "https://sub.mirror.test/post", "idhan.secrets()" ) == mirror );

	for ( const auto* hostname : { "otherexample.co.uk", "notexample.co.uk", "example.co.uk.evil.test" } )
	{
		const auto result {
			evaluateSecrets( registry, session, "https://" + std::string { hostname } + "/post", "idhan.secrets()" )
		};
		CHECK( result.isObject() );
		CHECK( result.empty() );
	}

	CHECK(
		evaluateSecrets(
			registry,
			session,
			"https://example.co.uk/post",
			"(() => { try { idhan.secrets('other.test'); return false; } catch (error) { return error instanceof TypeError; } })()" )
			.asBool() );

	session.fail = true;
	CHECK(
		evaluateSecrets(
			registry,
			session,
			"https://example.co.uk/post",
			"(() => { try { idhan.secrets(); return false; } catch (error) { return error.message === 'Secret store unavailable'; } })()" )
			.asBool() );
}

TEST_CASE( "Secret domains are canonical hostnames with no inferred parent", "[downloader][secrets]" )
{
	for ( const auto& [ input, expected ] :
	      { std::pair { "EXAMPLE.co.uk.", "example.co.uk" },
	        std::pair { "sub.example.co.uk", "sub.example.co.uk" },
	        std::pair { "b\xc3\xbc"
	                    "cher.example",
	                    "xn--bcher-kva.example" },
	        std::pair { "127.0.0.1", "127.0.0.1" } } )
	{
		const auto domain_e { downloader::secretDomain( input ) };
		REQUIRE( domain_e.has_value() );
		CHECK( domain_e.value() == expected );
	}

	for ( const auto* input :
	      { "",
	        "https://example.com",
	        "example.com/path",
	        "user@example.com",
	        "example.com:443",
	        "example.com?token=x" } )
	{
		CHECK_FALSE( downloader::secretDomain( input ).has_value() );
	}
}

} // namespace idhan::test
