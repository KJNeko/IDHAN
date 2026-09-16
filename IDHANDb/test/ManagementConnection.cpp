#include <catch2/catch_test_macros.hpp>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "IDHANDB.hpp"
#include "idhan/db/DBConnection.hpp"
#include "idhan/db/DBOperation.hpp"
#include "idhan/db/DBReactor.hpp"

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
