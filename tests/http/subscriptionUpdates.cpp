#include <catch2/catch_test_macros.hpp>

#include <future>
#include <thread>

#include "subscriptions/SubscriptionUpdateQueue.hpp"

namespace idhan::subscriptions::test
{
TEST_CASE(
	"Subscription count batches deduplicate sessions and flush the final isolated event",
	"[http][subscription-events]" )
{
	SubscriptionUpdateQueue queue { std::chrono::milliseconds { 10 } };
	queue.enqueue( 7 );
	queue.enqueue( 7 );
	queue.enqueue( 8 );
	CHECK( queue.waitForBatch( {} ) == std::vector< DownloadSessionID > { 7, 8 } );

	queue.enqueue( 9 );
	std::promise< std::vector< DownloadSessionID > > result;
	auto ready { result.get_future() };
	std::jthread worker {
		[ &queue, &result ]( const std::stop_token stop ) { result.set_value( queue.waitForBatch( stop ) ); }
	};
	REQUIRE( ready.wait_for( std::chrono::seconds { 1 } ) == std::future_status::ready );
	CHECK( ready.get() == std::vector< DownloadSessionID > { 9 } );
}

TEST_CASE( "Continuous subscription imports cannot postpone the flush deadline", "[http][subscription-events]" )
{
	SubscriptionUpdateQueue queue { std::chrono::milliseconds { 10 } };
	queue.enqueue( 7 );
	std::jthread producer {
		[ &queue ]( const std::stop_token stop )
		{
			while ( !stop.stop_requested() )
			{
				queue.enqueue( 7 );
				std::this_thread::sleep_for( std::chrono::milliseconds { 1 } );
			}
		}
	};
	std::promise< std::vector< DownloadSessionID > > result;
	auto ready { result.get_future() };
	std::jthread worker {
		[ &queue, &result ]( const std::stop_token stop ) { result.set_value( queue.waitForBatch( stop ) ); }
	};
	REQUIRE( ready.wait_for( std::chrono::seconds { 1 } ) == std::future_status::ready );
	CHECK( ready.get() == std::vector< DownloadSessionID > { 7 } );
}

TEST_CASE( "Stopping the subscription update worker wakes an idle queue", "[http][subscription-events]" )
{
	SubscriptionUpdateQueue queue;
	std::promise< std::vector< DownloadSessionID > > result;
	auto ready { result.get_future() };
	std::jthread worker {
		[ &queue, &result ]( const std::stop_token stop ) { result.set_value( queue.waitForBatch( stop ) ); }
	};
	worker.request_stop();
	REQUIRE( ready.wait_for( std::chrono::seconds { 1 } ) == std::future_status::ready );
	CHECK( ready.get().empty() );
}
} // namespace idhan::subscriptions::test
