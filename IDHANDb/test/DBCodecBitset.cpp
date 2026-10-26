#include <catch2/catch_test_macros.hpp>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <bitset>

#include "idhan/db/DBContext.hpp"

using namespace idhan::db;

TEST_CASE( "std::bitset<64> round-trips through a PostgreSQL bit(64) column", "[postgres][bitset]" )
{
	DBConfig config { DBConfig::defaults() };
	config.connections = 1;
	config.transaction_connections = 1;
	folly::CPUThreadPoolExecutor application { 1 };
	DBContext context { config, folly::getKeepAliveToken( &application ) };
	folly::coro::blockingWait( context.start() );

	auto transaction { folly::coro::blockingWait( context.transaction() ) };
	folly::coro::blockingWait(
		transaction.query( "CREATE TEMPORARY TABLE bitset_codec_test ( id integer PRIMARY KEY, value bit(64) )" )
			.result() );

	const std::bitset< 64 > value { 0xb44dc7b24dcb381cULL };

	{
		DBBindings bindings {};
		bindings.bind( std::int32_t { 1 } );
		bindings.bind( value );
		folly::coro::blockingWait(
			transaction.query( "INSERT INTO bitset_codec_test (id, value) VALUES ($1, $2)", std::move( bindings ) )
				.result() );
	}

	{
		DBBindings bindings {};
		bindings.bind( std::int32_t { 1 } );
		const auto rows { folly::coro::blockingWait(
			transaction.query( "SELECT value FROM bitset_codec_test WHERE id = $1", std::move( bindings ) )
				.rows< std::bitset< 64 > >() ) };
		REQUIRE( rows.size() == 1 );
		CHECK( std::get< 0 >( rows.row( 0 ) ) == value );
		CHECK( std::get< 0 >( rows.row( 0 ) ).to_ullong() == 0xb44dc7b24dcb381cULL );
	}

	{
		DBBindings bindings {};
		bindings.bind( std::int32_t { 2 } );
		bindings.bind( std::optional< std::bitset< 64 > > {} );
		folly::coro::blockingWait(
			transaction.query( "INSERT INTO bitset_codec_test (id, value) VALUES ($1, $2)", std::move( bindings ) )
				.result() );

		DBBindings select_bindings {};
		select_bindings.bind( std::int32_t { 2 } );
		const auto rows { folly::coro::blockingWait(
			transaction.query( "SELECT value FROM bitset_codec_test WHERE id = $1", std::move( select_bindings ) )
				.rows< std::optional< std::bitset< 64 > > >() ) };
		REQUIRE( rows.size() == 1 );
		CHECK_FALSE( std::get< 0 >( rows.row( 0 ) ).has_value() );
	}

	folly::coro::blockingWait( transaction.commit() );
	folly::coro::blockingWait( context.drain() );
}
