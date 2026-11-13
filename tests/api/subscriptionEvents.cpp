#include <catch2/catch_test_macros.hpp>

#include "JobEventStream.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{
TEST_CASE_METHOD(
	ServerFixture,
	"Subscription SSE sends compact changes, cascade removals and fresh reconnect baselines",
	"[api][subscriptions][subscription-events]" )
{
	JobEventStream stream { serverPort(), api().key(), "/events?filter=subscriptions" };
	const auto baseline { stream.waitFor( "snapshot" ) };
	REQUIRE( baseline.isObject() );
	REQUIRE( baseline[ "groups" ].isArray() );
	REQUIRE( baseline[ "items" ].isArray() );

	Json::Value group { Json::objectValue };
	group[ "name" ] = "Streamed subscriptions";
	group[ "generator" ] = "gelbooru:search";
	group[ "enabled" ] = false;
	group[ "interval_seconds" ] = 86400;
	const auto created { api().post( "/subscription_groups", group ) };
	REQUIRE( created.status == 201 );
	const auto group_id { created.json[ "id" ].asInt() };
	const auto path { std::format( "/subscription_groups/{}", group_id ) };
	const auto added { stream.waitFor( "patch" ) };
	REQUIRE( added[ "groups" ].size() == 1 );
	CHECK( added[ "groups" ][ 0 ][ "id" ] == group_id );
	CHECK( added[ "groups" ][ 0 ][ "name" ] == group[ "name" ] );
	CHECK_FALSE( added.isMember( "items" ) );

	group[ "name" ] = "Renamed";
	REQUIRE( api().patch( path, group ).status == 200 );
	const auto renamed { stream.waitFor( "patch" ) };
	REQUIRE( renamed[ "groups" ].size() == 1 );
	CHECK( renamed[ "groups" ][ 0 ].size() == 2 );
	CHECK( renamed[ "groups" ][ 0 ][ "name" ] == "Renamed" );

	Json::Value item { Json::objectValue };
	item[ "tags" ] = "artist_name";
	item[ "enabled" ] = false;
	const auto subscription { api().post( path + "/subscriptions", item ) };
	REQUIRE( subscription.status == 201 );
	const auto item_id { subscription.json[ "id" ].asInt() };
	const auto item_added { stream.waitFor( "patch" ) };
	REQUIRE( item_added[ "items" ].size() == 1 );
	CHECK( item_added[ "items" ][ 0 ][ "id" ] == item_id );
	CHECK( item_added[ "groups" ][ 0 ][ "subscription_count" ] == 1 );

	JobEventStream reconnected { serverPort(), api().key(), "/events?filter=subscriptions&session_ids=999" };
	const auto current { reconnected.waitFor( "snapshot" ) };
	CHECK( current[ "groups" ] == api().get( "/subscription_groups" ).json );
	CHECK( current[ "items" ] == api().get( "/subscriptions" ).json );

	REQUIRE( api().del( path ).status == 200 );
	const auto removed { stream.waitFor( "remove" ) };
	REQUIRE( removed[ "groups" ].size() == 1 );
	CHECK( removed[ "groups" ][ 0 ] == group_id );
	REQUIRE( removed[ "items" ].size() == 1 );
	CHECK( removed[ "items" ][ 0 ] == item_id );
	CHECK( reconnected.waitFor( "remove" ) == removed );
}
} // namespace idhan::test
