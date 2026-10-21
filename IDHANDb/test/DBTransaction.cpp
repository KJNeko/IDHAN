#include <catch2/catch_test_macros.hpp>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include "idhan/db/DBContext.hpp"

using namespace idhan::db;

TEST_CASE( "A dropped database transaction rolls back and returns its lease", "[postgres][transaction]" )
{
	DBConfig config { DBConfig::defaults() };
	config.connections = 1;
	config.transaction_connections = 1;
	folly::CPUThreadPoolExecutor application { 1 };
	DBContext context { config, folly::getKeepAliveToken( &application ) };
	folly::coro::blockingWait( context.start() );

	{
		auto transaction { folly::coro::blockingWait( context.transaction() ) };
		folly::coro::blockingWait(
			transaction.query( "CREATE TEMPORARY TABLE transaction_rollback_test ( id integer )" ).result() );
	}

	auto transaction { folly::coro::blockingWait( context.transaction() ) };
	auto table = folly::coro::blockingWait(
		transaction.query( "SELECT to_regclass('pg_temp.transaction_rollback_test')::text" )
			.rows< std::optional< std::string > >() );
	REQUIRE( table.size() == 1 );
	CHECK_FALSE( std::get< 0 >( table.row( 0 ) ).has_value() );
	folly::coro::blockingWait( transaction.commit() );
	folly::coro::blockingWait( context.drain() );
}
