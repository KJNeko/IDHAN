#include <catch2/catch_test_macros.hpp>

#include "JobEventStream.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{
static Json::Value subscriptionBaseline( JobEventStream& stream )
{
	const auto start { stream.waitFor( "snapshot_start" ) };
	REQUIRE( start.isObject() );
	REQUIRE( stream.waitFor( "snapshot_complete" ).isObject() );
	Json::Value baseline { Json::objectValue };
	baseline[ "groups" ] = Json::Value { Json::arrayValue };
	baseline[ "items" ] = Json::Value { Json::arrayValue };
	while ( true )
	{
		const auto chunk { stream.waitFor( "snapshot_chunk", {}, "", std::chrono::milliseconds { 0 } ) };
		if ( chunk.isNull() ) break;

		for ( const auto& group : chunk[ "groups" ] ) baseline[ "groups" ].append( group );
		for ( const auto& patch : chunk[ "items" ] )
		{
			auto item { start[ "item_defaults" ] };
			for ( const auto& name : patch.getMemberNames() ) item[ name ] = patch[ name ];
			baseline[ "items" ].append( std::move( item ) );
		}
	}
	return baseline;
}

TEST_CASE_METHOD(
	ServerFixture,
	"Subscription SSE sends compact changes, cascade removals and fresh reconnect baselines",
	"[api][subscriptions][subscription-events]" )
{
	JobEventStream stream { serverPort(), api().key(), "/events?filter=subscriptions" };
	const auto baseline { subscriptionBaseline( stream ) };
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
	const auto current { subscriptionBaseline( reconnected ) };
	CHECK( current[ "groups" ] == api().get( "/subscription_groups" ).json );
	REQUIRE( current[ "items" ].size() == 1 );
	CHECK( current[ "items" ][ 0 ][ "id" ] == item_id );
	CHECK( current[ "items" ][ 0 ][ "parameters" ] == subscription.json[ "parameters" ] );
	CHECK_FALSE( current[ "items" ][ 0 ].isMember( "active_root_id" ) );

	REQUIRE( api().del( path ).status == 200 );
	const auto removed { stream.waitFor( "remove" ) };
	REQUIRE( removed[ "groups" ].size() == 1 );
	CHECK( removed[ "groups" ][ 0 ] == group_id );
	REQUIRE( removed[ "items" ].size() == 1 );
	CHECK( removed[ "items" ][ 0 ] == item_id );
	CHECK( reconnected.waitFor( "remove" ) == removed );
}
} // namespace idhan::test
