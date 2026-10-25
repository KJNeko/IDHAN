#include <catch2/catch_test_macros.hpp>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"
#include "accumulateTagCounts.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag info", "[api][tags][info]" )
{
	GIVEN( "a tag" )
	{
		const auto samus { api().createTag( "Character", "Samus Aran" ) };

		WHEN( "its info is read by path" )
		{
			const auto info { api().get( "/tags/" + std::to_string( samus ) + "/info" ) };

			THEN( "it carries the folded namespace and subtag" )
			{
				REQUIRE( info.status == 200 );
				CHECK( info.json[ "tag_id" ].asInt() == samus );
				CHECK( info.json[ "namespace" ][ "text" ].asString() == "character" );
				CHECK( info.json[ "subtag" ][ "text" ].asString() == "samus aran" );
			}

			THEN( "it is used by nothing" )
			{
				CHECK( info.json[ "storage_count" ].asInt64() == 0 );
				CHECK( info.json[ "display_count" ].asInt64() == 0 );
				CHECK_FALSE( info.json.isMember( "items_count" ) );
			}
		}

		WHEN( "its info is read through the tag_id query form" )
		{
			const auto info { api().get( "/tags/info", { { "tag_id", std::to_string( samus ) } } ) };

			THEN( "the tag id is resolved" )
			{
				CHECK( info.status == 200 );
			}
		}

		WHEN( "its info is read through the tag_ids query form" )
		{
			const auto by_path { api().get( "/tags/" + std::to_string( samus ) + "/info" ) };
			const auto by_query { api().get( "/tags/info", { { "tag_ids", std::to_string( samus ) } } ) };

			THEN( "it answers the same as the path route" )
			{
				REQUIRE( by_query.status == 200 );
				CHECK( by_query.json == by_path.json );
			}
		}

		WHEN( "several ids are passed to the plural route" )
		{
			const auto info { api().get( "/tags/info", { { "tag_ids", std::to_string( samus ) + ",1" } } ) };

			THEN( "the scalar query parameter is rejected" )
			{
				CHECK( info.status == 400 );
			}
		}
	}

	GIVEN( "no tags" )
	{
		WHEN( "info is asked for an id that does not exist" )
		{
			const auto info { api().get( "/tags/9999/info" ) };

			// The domain endpoints answer 404 for the same mistake.
			THEN( "the answer is a bad request rather than a not found" )
			{
				CHECK( info.status == 400 );
			}
		}
	}
}

} // namespace idhan::test

namespace idhan::test
{

class TagCountFixture : public ServerFixture
{
  protected:

	TagDomainID first { api().createDomain( "info first" ) };
	TagDomainID second { api().createDomain( "info second" ) };
	TagDomainID outside { api().createDomain( "info outside" ) };
	TagDomainID empty { api().createDomain( "info empty" ) };
	TagID applied { api().createTag( "info", "applied" ) };
	TagID canonical { api().createTag( "info", "canonical" ) };
	TagID parent { api().createTag( "info", "parent" ) };
	TagID unused { api().createTag( "info", "unused" ) };

	ApiResponse info( const TagID tag, const std::vector< TagDomainID >& domains = {}, const bool query = false ) const
	{
		QueryParams params;
		for ( const auto domain : domains ) params.emplace_back( "tag_domains", std::to_string( domain ) );
		if ( query ) params.emplace_back( "tag_id", std::to_string( tag ) );
		return api().get( query ? "/tags/info" : "/tags/" + std::to_string( tag ) + "/info", params );
	}

	static void checkCounts( const Json::Value& row, const std::int64_t storage, const std::int64_t display )
	{
		REQUIRE( row[ "storage_count" ].isInt64() );
		REQUIRE( row[ "display_count" ].isInt64() );
		CHECK( row[ "storage_count" ].asInt64() == storage );
		CHECK( row[ "display_count" ].asInt64() == display );
		CHECK_FALSE( row.isMember( "items_count" ) );
		CHECK_FALSE( row.isMember( "item_count" ) );
	}

  public:

	TagCountFixture()
	{
		const auto records { api().createRecords( { 180, 181, 182, 183, 184 } ) };
		{
			pqxx::work transaction { db() };
			for ( const auto record : records )
				transaction.exec(
					"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, cluster_delete_time) "
					"VALUES ($1, 123, $2, now(), now())",
					pqxx::params { record, mime_ids::IMAGE_PNG } );
			transaction.commit();
		}
		for ( const auto domain : { first, second } )
		{
			REQUIRE( api().createAliases( domain, { { applied, canonical } } ).status == 200 );
			REQUIRE( api().createParents( domain, { { parent, canonical } } ).status == 200 );
		}
		REQUIRE( api().addTags( first, { records[ 0 ], records[ 1 ], records[ 2 ] }, { applied } ).status == 200 );
		REQUIRE( api().addTags( second, { records[ 0 ], records[ 1 ] }, { canonical } ).status == 200 );
		REQUIRE( api().addTags( outside, records, { canonical } ).status == 200 );
		pqxx::work accumulation { db() };
		accumulateTagCountsForTest( accumulation );
		accumulation.commit();
	}
};

TEST_CASE_METHOD(
	TagCountFixture,
	"Tag info returns domain-specific storage and resolved display counts",
	"[api][tags][info][counts]" )
{
	for ( const bool query : { false, true } )
	{
		const auto response { info( canonical, { first, second }, query ) };
		REQUIRE( response.status == 200 );
		checkCounts( response.json, 2, 3 );
		const auto& rows { response.json[ "domain_counts" ] };
		REQUIRE( rows.isArray() );
		REQUIRE( rows.size() == 2 );
		CHECK( rows[ 0 ][ "tag_domain_id" ].asInt() == first );
		checkCounts( rows[ 0 ], 0, 3 );
		CHECK( rows[ 1 ][ "tag_domain_id" ].asInt() == second );
		checkCounts( rows[ 1 ], 2, 2 );
		CHECK( info( canonical, { second, first, second }, query ).json == response.json );
	}
	const auto raw { info( applied, { first, second } ) };
	REQUIRE( raw.status == 200 );
	checkCounts( raw.json, 3, 0 );
	const auto inherited { info( parent, { first, second } ) };
	REQUIRE( inherited.status == 200 );
	checkCounts( inherited.json, 0, 3 );
	const auto single { info( canonical, { second } ) };
	REQUIRE( single.status == 200 );
	checkCounts( single.json, 2, 2 );
}

TEST_CASE_METHOD(
	TagCountFixture,
	"Tag info includes zero domains and defaults to all domains",
	"[api][tags][info][counts]" )
{
	const auto all { info( canonical ) };
	REQUIRE( all.status == 200 );
	checkCounts( all.json, 5, 5 );
	const auto domains { api().get( "/tags/domain/list" ) };
	REQUIRE( domains.status == 200 );
	CHECK( all.json[ "domain_counts" ].size() == domains.json.size() );
	TagDomainID previous {};
	for ( const auto& row : all.json[ "domain_counts" ] )
	{
		const auto id { row[ "tag_domain_id" ].asInt() };
		CHECK( id > previous );
		previous = static_cast< TagDomainID >( id );
	}
	for ( const auto tag : { canonical, unused } )
	{
		const auto zero { info( tag, { empty } ) };
		REQUIRE( zero.status == 200 );
		checkCounts( zero.json, 0, 0 );
		REQUIRE( zero.json[ "domain_counts" ].size() == 1 );
		CHECK( zero.json[ "domain_counts" ][ 0 ][ "tag_domain_id" ].asInt() == empty );
		checkCounts( zero.json[ "domain_counts" ][ 0 ], 0, 0 );
	}
	const auto zero { info( unused ) };
	REQUIRE( zero.status == 200 );
	checkCounts( zero.json, 0, 0 );
	CHECK( zero.json[ "domain_counts" ].size() == domains.json.size() );
}

TEST_CASE_METHOD( TagCountFixture, "Tag info validates domain selections on both routes", "[api][tags][info][counts]" )
{
	for ( const auto& path : { "/tags/" + std::to_string( canonical ) + "/info", std::string { "/tags/info" } } )
	{
		for ( const std::string value : { "0", "-1", "32768", "65537", "1.5", "abc", "" } )
		{
			CAPTURE( path, value );
			CHECK(
				api().get( path, { { "tag_id", std::to_string( canonical ) }, { "tag_domains", value } } ).status
			    == 400 );
		}
	}
	CHECK( info( canonical, { 32767 } ).status == 404 );
	CHECK( info( canonical, { first, 32767 }, true ).status == 404 );
}

} // namespace idhan::test
