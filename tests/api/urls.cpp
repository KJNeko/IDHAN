#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ServerFixture.hpp"

namespace idhan::test
{

using UrlDomainPair = std::pair< std::string, std::string >;

static Json::Value urlsBody( const std::vector< UrlDomainPair >& pairs )
{
	Json::Value body {};
	body[ "urls" ] = Json::Value { Json::arrayValue };

	for ( const auto& pair : pairs ) body[ "urls" ].append( pair.first );

	return body;
}

static std::vector< UrlDomainPair > storedUrlDomains( pqxx::connection& connection )
{
	pqxx::nontransaction tx { connection };
	std::vector< UrlDomainPair > pairs {};

	for ( const auto& [ url, domain ] : tx.query< std::string, std::string >(
			  "SELECT url, url_domain FROM urls JOIN url_domains USING (url_domain_id) ORDER BY url" ) )
	{
		pairs.emplace_back( url, domain );
	}

	return pairs;
}

static int tableCount( pqxx::connection& connection, const std::string_view table )
{
	pqxx::nontransaction tx { connection };
	return tx.query_value< int >( "SELECT count(*) FROM " + tx.quote_name( table ) );
}

SCENARIO_METHOD( ServerFixture, "URL batches preserve their hostname associations", "[api][urls]" )
{
	const std::vector< UrlDomainPair > pairs {
		{ "https://z.example/one", "z.example" },
		{ "https://a.example/two", "a.example" },
		{ "https://z.example/three", "z.example" },
		{ "https://example.com?view=1", "example.com" },
		{ "https://example.com#section", "example.com" },
		{ "https://user:password@secure.example:8443/private", "secure.example" },
		{ "https://port.example:9443/path", "port.example" },
		{ "https://[2001:db8::1]:8080/path", "2001:db8::1" },
	};
	const auto record_id { api().createRecord( 1 ) };
	const auto path { "/records/" + std::to_string( record_id ) + "/urls" };

	WHEN( "one batch contains reordered domains, repeated domains, and varied authorities" )
	{
		const auto added { api().post( path + "/add", urlsBody( pairs ) ) };

		THEN( "every URL is stored beside the hostname extracted from its own authority" )
		{
			REQUIRE( added.status == drogon::k200OK );

			auto expected { pairs };
			std::ranges::sort( expected );
			CHECK( storedUrlDomains( db() ) == expected );
		}

		THEN( "fetching the record returns every submitted URL" )
		{
			const auto fetched { api().get( path ) };
			REQUIRE( fetched.status == drogon::k200OK );
			REQUIRE( fetched.json.isArray() );

			std::vector< std::string > actual {};
			actual.reserve( fetched.json.size() );
			for ( const auto& url : fetched.json ) actual.emplace_back( url.asString() );

			std::vector< std::string > expected {};
			expected.reserve( pairs.size() );
			for ( const auto& pair : pairs ) expected.emplace_back( pair.first );
			std::ranges::sort( expected );

			CHECK( actual == expected );
		}

		AND_WHEN( "the same batch is posted again" )
		{
			const auto url_count { tableCount( db(), "urls" ) };
			const auto mapping_count { tableCount( db(), "url_mappings" ) };
			const auto added_again { api().post( path + "/add", urlsBody( pairs ) ) };

			THEN( "the operation remains idempotent" )
			{
				REQUIRE( added_again.status == drogon::k200OK );
				CHECK( tableCount( db(), "urls" ) == url_count );
				CHECK( tableCount( db(), "url_mappings" ) == mapping_count );
			}
		}
	}
}

} // namespace idhan::test
