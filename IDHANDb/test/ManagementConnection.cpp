#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "IDHANDB.hpp"
#include "idhan/db/DBConnection.hpp"
#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"

// Folly includes glog's CHECK; install Catch's assertion macros after those headers.
#include <catch2/catch_test_macros.hpp>

using namespace idhan::db;

static DBConfig testConfig()
{
	const auto* connection = std::getenv( "IDHAN_PQ_TEST_CONNINFO" );
	if ( !connection ) return DBConfig {};

	PQconninfoOption* options = PQconninfoParse( connection, nullptr );
	if ( !options ) throw std::runtime_error( "Invalid PostgreSQL test conninfo" );

	DBConfig config;
	if ( const auto* user = std::getenv( "USER" ) ) config.user = user;
	for ( auto* option = options; option->keyword; ++option )
	{
		if ( option->val == nullptr ) continue;

		const std::string_view key = option->keyword;
		if ( key == "host" )
			config.host = option->val;
		else if ( key == "port" )
			config.port = static_cast< std::uint16_t >( std::stoi( option->val ) );
		else if ( key == "dbname" )
			config.database = option->val;
		else if ( key == "user" )
			config.user = option->val;
		else if ( key == "password" )
			config.password = option->val;
	}
	PQconninfoFree( options );
	return config;
}

TEST_CASE( "Test basic management connection", "[postgres][management]" )
{
	DBReactor reactor { 1 };
	reactor.start();
	folly::CPUThreadPoolExecutor application { 1 };
	auto connection = std::make_shared< DBConnection >( 1, ConnectionRole::management, testConfig(), reactor );
	folly::coro::blockingWait( connection->connect() );

	const auto makeTask = [ connection, &application ]( std::string sql, DBBindings bindings = {} )
	{
		auto operation = std::make_shared< DBOperation >(
			std::move( sql ), std::move( bindings ), QueryOptions {}, folly::getKeepAliveToken( &application ) );
		operation->setStarter( [ connection ]( std::shared_ptr< DBOperation > queued )
		                       { connection->enqueue( std::move( queued ) ); } );
		return DBTask { std::move( operation ) };
	};

	folly::coro::blockingWait( makeTask( "DROP TABLE IF EXISTS management_connection_test" ) );
	folly::coro::blockingWait( makeTask( "CREATE TABLE management_connection_test ( id integer )" ) );

	folly::coro::blockingWait( makeTask( "BEGIN" ) );
	folly::coro::blockingWait( makeTask( "ALTER TABLE management_connection_test ADD COLUMN label text" ) );
	folly::coro::blockingWait( makeTask( "COMMIT" ) );

	DBBindings table_name;
	table_name.bind( std::string { "management_connection_test" } );
	auto columns = folly::coro::blockingWait(
		makeTask(
			"SELECT column_name::text FROM information_schema.columns WHERE table_name = $1 ORDER BY ordinal_position",
			std::move( table_name ) )
			.rows< std::string >() );

	REQUIRE( columns.size() == 2 );
	CHECK( std::get< 0 >( columns.row( 0 ) ) == "id" );
	CHECK( std::get< 0 >( columns.row( 1 ) ) == "label" );

	folly::coro::blockingWait( makeTask( "DROP TABLE management_connection_test" ) );

	connection.reset();
	reactor.stop();
}

TEST_CASE( "Slow database queries log first and final result times", "[postgres][management][slow-query]" )
{
	using namespace std::chrono_literals;
	CHECK( DBConfig {}.slow_query_threshold == 1000ms );

	std::ostringstream output;
	auto sink { std::make_shared< spdlog::sinks::ostream_sink_mt >( output ) };
	auto logger { std::make_shared< spdlog::logger >( "slow-query-test", sink ) };
	logger->set_level( spdlog::level::debug );
	auto previous_logger { spdlog::default_logger() };

	struct RestoreLogger
	{
		std::shared_ptr< spdlog::logger > previous;

		~RestoreLogger() { spdlog::set_default_logger( std::move( previous ) ); }
	} restore { std::move( previous_logger ) };

	spdlog::set_default_logger( logger );

	DBReactor reactor { 1 };
	reactor.start();
	folly::CPUThreadPoolExecutor application { 1 };
	const auto config { testConfig() };
	auto connection = std::make_shared< DBConnection >( 1, ConnectionRole::management, config, reactor );
	folly::coro::blockingWait( connection->connect() );

	auto operation = std::make_shared< DBOperation >(
		" \r\nSELECT\n\tpg_sleep(1.15)\n", DBBindings {}, QueryOptions {}, folly::getKeepAliveToken( &application ) );
	operation->setStarter( [ connection ]( std::shared_ptr< DBOperation > queued )
	                       { connection->enqueue( std::move( queued ) ); } );
	folly::coro::blockingWait( DBTask { std::move( operation ) }.result() );
	auto fast_operation = std::make_shared< DBOperation >(
		"-- keep this comment on its own line\r\n\tSELECT\n    1\n",
		DBBindings {},
		QueryOptions {},
		folly::getKeepAliveToken( &application ) );
	fast_operation->setStarter( [ connection ]( std::shared_ptr< DBOperation > queued )
	                            { connection->enqueue( std::move( queued ) ); } );
	const auto rows { folly::coro::blockingWait( DBTask { std::move( fast_operation ) }.rows< std::int32_t >() ) };
	REQUIRE( rows.size() == 1 );
	CHECK( std::get< 0 >( rows.row( 0 ) ) == 1 );
	folly::coro::blockingWait( connection->drain() );

	const auto logged { output.str() };
	CHECK( logged.find( "[warning] PostgreSQL query first result:" ) != std::string::npos );
	CHECK( logged.find( "[warning] PostgreSQL query complete: first result" ) != std::string::npos );
	CHECK( logged.find( "SELECT pg_sleep(1.15)" ) != std::string::npos );
	CHECK( logged.find( "[debug] PostgreSQL query first result:" ) != std::string::npos );
	CHECK( logged.find( "[debug] PostgreSQL query complete: first result" ) != std::string::npos );
	CHECK( logged.find( "SELECT 1" ) != std::string::npos );
	CHECK( logged.find( "-- keep this comment on its own line SELECT 1" ) != std::string::npos );
	CHECK( logged.find_first_of( "\r\t" ) == std::string::npos );
	std::istringstream lines { logged };
	std::string line;
	std::size_t line_count {};
	while ( std::getline( lines, line ) )
	{
		CHECK( line.find( "PostgreSQL query " ) != std::string::npos );
		++line_count;
	}
	CHECK( line_count == 4 );

	connection.reset();
	reactor.stop();
}
