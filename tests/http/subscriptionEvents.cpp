#include <folly/coro/BlockingWait.h>

#include "idhan/http/streaming/SSEEventHub.hpp"
#include "subscriptions/SubscriptionEventState.hpp"
#undef CHECK
#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <sstream>

namespace idhan::subscriptions::test
{
static Json::Value document( const http::ValidatedJson& json )
{
	std::istringstream input { std::string { json.text() } };
	Json::Value value;
	input >> value;
	return value;
}

TEST_CASE( "Subscription events send changed fields and explicit nulls only", "[http][subscription-events]" )
{
	SubscriptionEventState state;
	db::SubscriptionGroup group { .id = 7, .name = "Artists" };
	db::Subscription item { .id = 42, .group_id = 7, .tags = "artist", .last_error = "failed" };
	REQUIRE( state.reconcile( { group }, { item } ).size() == 1 );
	CHECK( state.reconcile( { group }, { item } ).empty() );
	item.imported_count = 18;
	item.last_error.reset();
	const auto changes { state.reconcile( { group }, { item } ) };
	REQUIRE( changes.size() == 1 );
	const auto patch { document( changes.front() ) };
	CHECK( patch[ "type" ] == "patch" );
	CHECK_FALSE( patch.isMember( "groups" ) );
	CHECK_FALSE( patch.isMember( "settings" ) );
	REQUIRE( patch[ "items" ].size() == 1 );
	const auto& fields { patch[ "items" ][ 0 ] };
	CHECK( fields.size() == 3 );
	CHECK( fields[ "id" ] == 42 );
	CHECK( fields[ "imported_count" ] == 18 );
	CHECK( fields.isMember( "last_error" ) );
	CHECK( fields[ "last_error" ].isNull() );
	CHECK( changes.front().text().size() < 110 );
}

TEST_CASE(
	"Subscription events preserve groups outside a scoped refresh and detect cascade deletion",
	"[http][subscription-events]" )
{
	SubscriptionEventState state;
	db::SubscriptionGroup first { .id = 7, .name = "Artists" };
	db::SubscriptionGroup second { .id = 8, .name = "Other" };
	db::Subscription item { .id = 42, .group_id = 7 };
	db::Subscription other { .id = 43, .group_id = 8 };
	(void)state.reconcile( { first, second }, { item, other } );
	const auto changes { state.reconcile( {}, {}, 7 ) };
	REQUIRE( changes.size() == 1 );
	const auto removal { document( changes.front() ) };
	CHECK( removal[ "type" ] == "remove" );
	CHECK( removal[ "groups" ][ 0 ] == 7 );
	CHECK( removal[ "items" ][ 0 ] == 42 );
	const auto snapshots { state.snapshots() };
	REQUIRE( snapshots.size() == 3 );
	const auto snapshot { document( snapshots[ 1 ] ) };
	REQUIRE( snapshot[ "groups" ].size() == 1 );
	CHECK( snapshot[ "groups" ][ 0 ][ "id" ] == 8 );
	CHECK( snapshot[ "items" ][ 0 ][ "id" ] == 43 );
	CHECK( state.reconcile( {}, {}, 7 ).empty() );
}

TEST_CASE( "New subscription entities contain the complete state", "[http][subscription-events]" )
{
	SubscriptionEventState state;
	(void)state.reconcile( {}, {} );
	db::SubscriptionGroup group { .id = 7, .name = "Artists" };
	db::Subscription item { .id = 42, .group_id = 7, .parameters = { { "tags", "artist" } } };
	const auto changes { state.reconcile( { group }, { item } ) };
	REQUIRE( changes.size() == 1 );
	const auto patch { document( changes.front() ) };
	CHECK( patch[ "items" ][ 0 ][ "parameters" ][ "tags" ] == "artist" );
	CHECK( patch[ "items" ][ 0 ].isMember( "active_session_id" ) );
	CHECK( patch[ "groups" ][ 0 ][ "name" ] == "Artists" );
}

TEST_CASE (
"Subscription snapshots bound every chunk and preserve all rows"
,
"[http][subscription-events]"
)
{
	SubscriptionEventState state;
	std::vector< db::SubscriptionGroup > groups;
	std::vector< db::Subscription > items;
	for ( SubscriptionGroupID id { 1 }; id <= static_cast< SubscriptionGroupID >( snapshot_chunk_size ) + 1; ++id )
		groups.push_back( { .id = id, .name = "Artists" } );
	for ( SubscriptionID id { 1 }; id <= static_cast< SubscriptionID >( snapshot_chunk_size ) + 2; ++id )
		items.push_back( { .id = id, .group_id = 1, .parameters = { { "tags", "artist" } } } );
	(void)state.reconcile( std::move( groups ), std::move( items ) );
	const auto snapshots { state.snapshots() };
	REQUIRE( snapshots.size() == 5 );
	CHECK( document( snapshots.front() )[ "type" ] == "snapshot_start" );
	CHECK( document( snapshots.back() )[ "type" ] == "snapshot_complete" );
	SubscriptionGroupID group_id {};
	SubscriptionID item_id {};
	for ( std::size_t index { 1 }; index + 1 < snapshots.size(); ++index )
	{
		const auto chunk { document( snapshots[ index ] ) };
		CHECK( chunk[ "type" ] == "snapshot_chunk" );
		const auto count { chunk[ "groups" ].size() + chunk[ "items" ].size() };
		CHECK( count > 0 );
		CHECK( count <= snapshot_chunk_size );
		for ( const auto& group : chunk[ "groups" ] )
		{
			CHECK( group[ "id" ].asInt64() == ++group_id );
			CHECK( group[ "name" ] == "Artists" );
		}
		for ( const auto& item : chunk[ "items" ] )
		{
			CHECK( item[ "id" ].asInt64() == ++item_id );
			CHECK( item[ "parameters" ][ "tags" ] == "artist" );
			CHECK( item.isMember( "active_session_id" ) );
		}
	}
	CHECK( group_id == snapshot_chunk_size + 1 );
	CHECK( item_id == snapshot_chunk_size + 2 );
}

TEST_CASE (
"Exact subscription chunk boundaries do not emit an empty chunk"
,
"[http][subscription-events]"
)
{
	SubscriptionEventState state;
	std::vector< db::Subscription > items;
	for ( SubscriptionID id { 1 }; id <= static_cast< SubscriptionID >( snapshot_chunk_size ); ++id )
		items.push_back( { .id = id, .group_id = 7 } );
	(void)state.reconcile( {}, std::move( items ) );
	const auto snapshots { state.snapshots() };
	REQUIRE( snapshots.size() == 3 );
	CHECK( document( snapshots[ 1 ] )[ "items" ].size() == snapshot_chunk_size );
	CHECK( document( snapshots.back() )[ "type" ] == "snapshot_complete" );
}

TEST_CASE (
"Empty subscription snapshots still reset and complete"
,
"[http][subscription-events]"
)
{
	SubscriptionEventState state;
	const auto snapshots { state.snapshots() };
	REQUIRE( snapshots.size() == 2 );
	CHECK( document( snapshots.front() )[ "type" ] == "snapshot_start" );
	CHECK( document( snapshots.back() )[ "type" ] == "snapshot_complete" );
}

TEST_CASE( "Subscription baselines discard only covered subscription updates", "[http][subscription-events]" )
{
	http::SSEEventHub hub;
	auto stream { hub.subscribeEvents( {}, {}, false ) };
	const auto value { subscriptionJson( db::SubscriptionGroup {} ) };
	hub.publish( "subscriptions.changed", "subscriptions", value, { .retain_snapshot = false, .coalesce = false } );
	hub.publish(
		"downloader.session_state", "42", value, { .retain_snapshot = false, .coalesce = false, .session = "42" } );
	const auto checkpoint { stream.checkpoint() };
	hub.publish( "subscriptions.changed", "subscriptions", value, { .retain_snapshot = false, .coalesce = false } );
	SubscriptionEventState state;
	(void)state.reconcile( { db::SubscriptionGroup { .id = 7 } }, { db::Subscription { .id = 42, .group_id = 7 } } );
	std::vector< http::SSEInitialEvent > initial;
	for ( auto& snapshot : state.snapshots() )
		initial.push_back( { "subscriptions.snapshot", "subscriptions", std::move( snapshot ) } );
	stream.prependSnapshots( std::move( initial ), checkpoint, "subscriptions" );
	hub.shutdown();
	for ( const auto* type :
	      { "subscriptions.snapshot",
	        "subscriptions.snapshot",
	        "subscriptions.snapshot",
	        "downloader.session_state",
	        "subscriptions.changed" } )
	{
		const auto event { folly::coro::blockingWait( stream.next() ) };
		REQUIRE( event.has_value() );
		CHECK( event->type == type );
	}
	CHECK( folly::coro::blockingWait( stream.next() )->kind == http::SSEEvent::Kind::shutdown );
}
} // namespace idhan::subscriptions::test
