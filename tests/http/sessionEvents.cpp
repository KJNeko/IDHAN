#include "idhan/http/streaming/SSEEventHub.hpp"
#include <folly/coro/BlockingWait.h>
#undef CHECK
#include <catch2/catch_test_macros.hpp>

#include <string>

namespace idhan::http::test
{
static ValidatedJson payload( const std::string_view type )
{
	return ValidatedJson::parse( std::format( R"({{"type":"{}"}})", type ) ).value();
}

static SSEPublishOptions sessionEvent( const std::string_view session )
{
	return SSEPublishOptions { .retain_snapshot = false, .coalesce = false, .session = std::string { session } };
}

TEST_CASE( "Stream subscriptions filter session events but keep unscoped ones", "[http][session-events]" )
{
	SSEEventHub hub;
	auto subscription { hub.subscribeEvents( { "downloader" }, {}, false, { "42" } ) };
	hub.publish( "downloader.session_state", "7", payload( "session_state" ), sessionEvent( "7" ) );
	hub.publish( "downloader.session_state", "42", payload( "session_state" ), sessionEvent( "42" ) );
	hub.publish( "job.status", "1", payload( "status" ), { .retain_snapshot = false } );
	hub.publish( "downloader.rate_limit", "lane", payload( "rate_limit" ), { .retain_snapshot = false } );
	hub.publish( "downloader.url_created", "42", payload( "url_created" ), sessionEvent( "42" ) );

	for ( const auto* type : { "downloader.session_state", "downloader.rate_limit", "downloader.url_created" } )
	{
		const auto event { folly::coro::blockingWait( subscription.next() ) };
		REQUIRE( event.has_value() );
		CHECK( event->type == type );
	}
	hub.shutdown();
	CHECK( folly::coro::blockingWait( subscription.next() )->kind == SSEEvent::Kind::shutdown );
}

TEST_CASE( "Snapshots replace session events queued before their checkpoint", "[http][session-events]" )
{
	SSEEventHub hub;
	auto subscription { hub.subscribeEvents( { "downloader" }, {}, false, {} ) };
	hub.publish( "downloader.url_updated", "42", payload( "url_updated" ), sessionEvent( "42" ) );
	hub.publish( "downloader.rate_limit", "lane", payload( "rate_limit" ), { .retain_snapshot = false } );
	const auto checkpoint { subscription.checkpoint() };
	hub.publish( "downloader.url_created", "42", payload( "url_created" ), sessionEvent( "42" ) );

	std::vector< SSEInitialEvent > initial;
	initial.push_back( { "downloader.session_snapshot", "42", payload( "session_snapshot" ), "42" } );
	subscription.prependSnapshots( std::move( initial ), checkpoint );

	for ( const auto* type : { "downloader.session_snapshot", "downloader.rate_limit", "downloader.url_created" } )
	{
		const auto event { folly::coro::blockingWait( subscription.next() ) };
		REQUIRE( event.has_value() );
		CHECK( event->type == type );
	}
	hub.shutdown();
	CHECK( folly::coro::blockingWait( subscription.next() )->kind == SSEEvent::Kind::shutdown );
}

TEST_CASE( "Slow clients are disconnected once queued updates exceed the byte cap", "[http][session-events]" )
{
	SSEEventHub hub;
	auto subscription { hub.subscribeEvents( { "downloader" }, {}, false, {} ) };
	const std::string padding( 64 * 1024, 'x' );
	const auto large { ValidatedJson::parse( std::format( R"({{"type":"url_created","url":"{}"}})", padding ) ) };
	REQUIRE( large.has_value() );

	const auto needed { SSEEventHub::max_pending_update_bytes / padding.size() + 2 };
	for ( std::size_t index {}; index < needed; ++index )
		hub.publish( "downloader.url_created", "42", large.value(), sessionEvent( "42" ) );

	CHECK_FALSE( folly::coro::blockingWait( subscription.next() ).has_value() );
}

TEST_CASE( "Snapshots do not count toward the byte cap", "[http][session-events]" )
{
	SSEEventHub hub;
	auto subscription { hub.subscribeEvents( { "downloader" }, {}, false, {} ) };
	const std::string padding( 64 * 1024, 'x' );
	const auto large { ValidatedJson::parse( std::format( R"({{"type":"urls_snapshot","url":"{}"}})", padding ) ) };
	REQUIRE( large.has_value() );

	std::vector< SSEInitialEvent > initial;
	const auto count { SSEEventHub::max_pending_update_bytes / padding.size() + 2 };
	for ( std::size_t index {}; index < count; ++index )
		initial.push_back( { "downloader.urls_snapshot", "42", large.value(), "42" } );
	subscription.prependSnapshots( std::move( initial ), subscription.checkpoint() );
	hub.publish( "downloader.url_created", "42", payload( "url_created" ), sessionEvent( "42" ) );

	for ( std::size_t index {}; index < count; ++index )
		REQUIRE( folly::coro::blockingWait( subscription.next() ).has_value() );

	const auto live { folly::coro::blockingWait( subscription.next() ) };
	REQUIRE( live.has_value() );
	CHECK( live->type == "downloader.url_created" );
}
} // namespace idhan::http::test
