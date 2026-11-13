#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

TEST_CASE_METHOD(
	ServerFixture,
	"Reflected batch tag requests accept null optionals and mixed tag representations",
	"[api][parameters][reflection]" )
{
	const auto domain { api().createDomain( "reflected-requests" ) };
	const auto record { api().createRecord( 2302 ) };
	const auto tag { api().createTag( "artist", "existing" ) };
	const QueryParams query { { "tag_domain_id", std::to_string( domain ) } };
	Json::Value body { Json::objectValue };
	body[ "records" ] = Json::Value { Json::arrayValue };
	body[ "records" ].append( record );
	body[ "tags" ] = Json::nullValue;
	body[ "sets" ] = Json::nullValue;
	REQUIRE( api().post( "/records/tags/add", body, query ).status == 200 );

	body[ "tags" ] = Json::Value { Json::arrayValue };
	body[ "tags" ].append( tag );
	body[ "tags" ].append( "artist:from-string" );
	Json::Value object { Json::objectValue };
	object[ "namespace" ] = "artist";
	object[ "subtag" ] = "from-object";
	body[ "tags" ].append( object );
	body[ "sets" ] = Json::Value { Json::arrayValue };
	Json::Value set { Json::arrayValue };
	set.append( tag );
	body[ "sets" ].append( set );
	REQUIRE( api().post( "/records/tags/add", body, query ).status == 200 );

	{
		pqxx::read_transaction tx { db() };
		CHECK(
			tx.query_value< int >(
				"SELECT count(*) FROM tag_mappings WHERE record_id = $1 AND tag_domain_id = $2",
				pqxx::params { record, domain } )
		    == 3 );
	}
	body[ "tags" ] = false;
	CHECK( api().post( "/records/tags/add", body, query ).status == 400 );
	body[ "tags" ] = Json::nullValue;
	body[ "records" ] = Json::nullValue;
	CHECK( api().post( "/records/tags/add", body, query ).status == 400 );
}

} // namespace idhan::test
