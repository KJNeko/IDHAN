#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "IDHANDB.hpp"
#include "idhan/db/DBConnection.hpp"
#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"

// Folly includes glog's CHECK; install Catch's assertion macros after those headers.
#include <catch2/catch_test_macros.hpp>

using namespace idhan::db;

static DBConfig testConfig()
{
	DBConfig config {};
	const auto* connection = std::getenv( "IDHAN_PQ_TEST_CONNINFO" );
	if ( !connection ) return config;

	PQconninfoOption* options = PQconninfoParse( connection, nullptr );
	if ( !options ) throw std::runtime_error( "Invalid PostgreSQL test conninfo" );

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

TEST_CASE( "Database connection drains backlogs across budgets and sends isolated queries", "[postgres][reactor]" )
{
	auto role { ConnectionRole::normal };
	SECTION( "pipeline" )
	{
		role = ConnectionRole::normal;
	}
	SECTION( "management" )
	{
		role = ConnectionRole::management;
	}

	DBReactor reactor { 1 };
	reactor.start();
	folly::CPUThreadPoolExecutor application { 1 };
	auto connection { std::make_shared< DBConnection >( 1, role, testConfig(), reactor ) };
	std::vector< std::shared_ptr< DBOperation > > operations;
	constexpr auto count { connection_operation_budget * 3 + 1 };
	constexpr auto failure_index { connection_operation_budget + 3 };
	for ( std::size_t index = 0; index < count; ++index )
	{
		auto operation { std::make_shared< DBOperation >(
			index == failure_index ? "SELECT 1 / 0" : std::format( "SELECT {}::integer", index ),
			DBBindings {},
			QueryOptions {},
			folly::getKeepAliveToken( &application ) ) };
		operations.push_back( operation );
		// Queue before connecting so the backlog deterministically exceeds a connection work budget.
		connection->enqueue( std::move( operation ) );
	}
	folly::coro::blockingWait( connection->connect() );

	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 10 } };
	for ( std::size_t index = 0; index < count; ++index )
	{
		const auto& operation { operations[ index ] };
		while ( operation->phase() != OperationPhase::completed && std::chrono::steady_clock::now() < deadline )
			std::this_thread::sleep_for( std::chrono::milliseconds { 1 } );

		REQUIRE( operation->phase() == OperationPhase::completed );
		if ( index == failure_index )
		{
			CHECK_THROWS_AS( folly::coro::blockingWait( DBTask { operation }.result() ), DBQueryError );
			continue;
		}

		const auto rows { folly::coro::blockingWait( DBTask { operation }.rows< std::int32_t >() ) };
		REQUIRE( rows.size() == 1 );
		CHECK( std::get< 0 >( rows.row( 0 ) ) == static_cast< std::int32_t >( index ) );
	}
	folly::coro::blockingWait( connection->drain() );
	CHECK( connection->stats().unfinished == 0 );

	auto isolated { std::make_shared< DBOperation >(
		"SELECT 42::integer", DBBindings {}, QueryOptions {}, folly::getKeepAliveToken( &application ) ) };
	connection->enqueue( isolated );
	const auto isolated_deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
	while ( isolated->phase() != OperationPhase::completed && std::chrono::steady_clock::now() < isolated_deadline )
		std::this_thread::sleep_for( std::chrono::milliseconds { 1 } );

	REQUIRE( isolated->phase() == OperationPhase::completed );
	const auto rows { folly::coro::blockingWait( DBTask { isolated }.rows< std::int32_t >() ) };
	REQUIRE( rows.size() == 1 );
	CHECK( std::get< 0 >( rows.row( 0 ) ) == 42 );
	folly::coro::blockingWait( connection->drain() );
	reactor.stop();
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

	folly::coro::blockingWait( makeTask(
		//language=postgresql
		R"SQL(
			DROP TABLE IF EXISTS management_connection_test
		)SQL" ) );
	folly::coro::blockingWait( makeTask(
		//language=postgresql
		R"SQL(
			CREATE TABLE management_connection_test (
			    id integer
			)
		)SQL" ) );

	folly::coro::blockingWait( makeTask(
		//language=postgresql
		R"SQL(
			BEGIN
		)SQL" ) );
	folly::coro::blockingWait( makeTask(
		//language=postgresql
		R"SQL(
			ALTER TABLE management_connection_test
			    ADD COLUMN label text
		)SQL" ) );
	folly::coro::blockingWait( makeTask(
		//language=postgresql
		R"SQL(
			COMMIT
		)SQL" ) );

	DBBindings table_name;
	table_name.bind( std::string { "management_connection_test" } );
	auto columns = folly::coro::blockingWait(
		makeTask(
			//language=postgresql
			R"SQL(
				SELECT
				    column_name::text
				FROM information_schema.columns
				WHERE table_name = $1
				ORDER BY
				    ordinal_position
			)SQL",
			std::move( table_name ) )
			.rows< std::string >() );

	REQUIRE( columns.size() == 2 );
	CHECK( std::get< 0 >( columns.row( 0 ) ) == "id" );
	CHECK( std::get< 0 >( columns.row( 1 ) ) == "label" );

	folly::coro::blockingWait( makeTask(
		//language=postgresql
		R"SQL(
			DROP TABLE management_connection_test
		)SQL" ) );

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
