#include "db/bandwidth.hpp"

#include <folly/executors/CPUThreadPoolExecutor.h>

#include <chrono>
#include <memory>
#include <type_traits>

#include "downloader/BandwidthTree.hpp"
#include "idhan/db/DBContext.hpp"

#include <catch2/catch_test_macros.hpp>
#include "MigratedSchema.hpp"

namespace idhan::test
{
class BandwidthSchema : public MigratedSchema
{
	folly::CPUThreadPoolExecutor m_executor { 1 };
	std::unique_ptr< db::DBContext > m_context {};

  public:

	BandwidthSchema()
	{
		const PostgresSettings settings;
		db::DBConfig config;
		config.host = settings.host;
		config.port = static_cast< std::uint16_t >( std::stoul( settings.port ) );
		config.database = settings.database;
		config.user = settings.user;
		config.password = settings.password;
		config.schema = schema();
		config.connections = 1;
		config.transaction_connections = 1;
		config.worker_threads = 2;
		config.statement_timeout = std::chrono::seconds { 5 };
		m_context = std::make_unique< db::DBContext >( config, folly::getKeepAliveToken( &m_executor ) );
		run( m_context->start() );
	}

	~BandwidthSchema() { run( m_context->drain() ); }

	template < typename T >
	auto start( runtime::Task< T > task )
	{
		return folly::coro::co_withExecutor( folly::getKeepAliveToken( &m_executor ), std::move( task ) ).start();
	}

	template < typename T >
	T run( runtime::Task< T > task )
	{
		auto future { start( std::move( task ) ) };
		if constexpr ( std::is_void_v< T > )
			std::move( future ).get();
		else
			return std::move( future ).get();
	}

	db::DBContext& context() { return *m_context; }
	pqxx::connection& sql() { return connection(); }


};

struct BandwidthNode
{
	std::string host;
	std::int64_t bytes_received {};
	std::vector< BandwidthNode > children;
};

TEST_CASE( "Bandwidth children retain only their exact host totals", "[bandwidth][tree]" )
{
	const auto roots { downloader::bandwidthTree< BandwidthNode >(
		{ { "img4.gelbooru.com", 20 },
		  { "gelbooru.com", 100 },
		  { "a.img4.gelbooru.com", 5 },
		  { "cdn.example.com", 8 },
		  { "127.0.0.1", 7 },
		  { "[::1]", 9 } } ) };
	REQUIRE( roots.size() == 4 );
	CHECK( roots[ 0 ].host == "127.0.0.1" );
	CHECK( roots[ 1 ].host == "[::1]" );
	const auto& synthetic { roots[ 2 ] };
	CHECK( synthetic.host == "example.com" );
	CHECK( synthetic.bytes_received == 0 );
	REQUIRE( synthetic.children.size() == 1 );
	CHECK( synthetic.children[ 0 ].bytes_received == 8 );
	const auto& parent { roots[ 3 ] };
	CHECK( parent.host == "gelbooru.com" );
	CHECK( parent.bytes_received == 100 );
	REQUIRE( parent.children.size() == 1 );
	const auto& child { parent.children[ 0 ] };
	CHECK( child.host == "img4.gelbooru.com" );
	CHECK( child.bytes_received == 20 );
	REQUIRE( child.children.size() == 1 );
	CHECK( child.children[ 0 ].bytes_received == 5 );
}

TEST_CASE_METHOD(
	BandwidthSchema,
	"Bandwidth batches are atomic and respect Unix time boundaries",
	"[db][bandwidth]" )
{
	const std::vector< downloader::BandwidthLog > logs {
		{ "gelbooru.com", 99, 90 },
		{ "gelbooru.com", 100, 10 },
		{ "gelbooru.com", 150, 20 },
		{ "gelbooru.com", 150, 20 },
		{ "img4.gelbooru.com", 150, 40 },
		{ "gelbooru.com", 200, 80 }
	};
	auto invalid { logs };
	invalid.back().bytes_received = -1;
	CHECK_THROWS( run( db::storeBandwidth( context(), invalid ) ) );
	CHECK( run( db::bandwidthTotals( context(), 0, 300 ) ).empty() );
	run( db::storeBandwidth( context(), logs ) );
	const std::vector< downloader::BandwidthLog > later_logs { { "gelbooru.com", 300, 1 }, { "gelbooru.com", 150, 7 }, { "img4.gelbooru.com", 150, 3 } };
	run( db::storeBandwidth( context(), later_logs ) );
	{
		pqxx::read_transaction read { sql() };
		CHECK( read.query_value< int >( "SELECT count(*) FROM bandwidth_hosts" ) == 2 );
		CHECK( read.query_value< int >( "SELECT count(DISTINCT host_id) FROM bandwidth_logs" ) == 2 );
		CHECK( read.query_value< int >( "SELECT count(*) FROM bandwidth_logs" ) == 6 );
	}
	const auto totals { run( db::bandwidthTotals( context(), 100, 200 ) ) };
	REQUIRE( totals.size() == 2 );
	CHECK( totals[ 0 ].host == "gelbooru.com" );
	CHECK( totals[ 0 ].bytes_received == 57 );
	CHECK( totals[ 1 ].host == "img4.gelbooru.com" );
	CHECK( totals[ 1 ].bytes_received == 43 );
	CHECK( run( db::bandwidthTotals( context(), 201, 300 ) ).empty() );
}
TEST_CASE( "Bandwidth host migrations preserve existing logs", "[db][bandwidth][migration]" )
{
	pqxx::connection connection { connectionString() };
	// The schema and every migration change roll back together when this test ends.
	pqxx::work transaction { connection };
	transaction.exec( "CREATE SCHEMA bandwidth_host_migration_test" );
	transaction.exec( "SET LOCAL search_path = bandwidth_host_migration_test, public" );
	static constexpr char create_logs[] {
#embed "../../IDHANMigration/src/table_bandwidth_logs/445-create.sql" suffix(,)
		'\0'
	};
	static constexpr char create_hosts[] {
#embed "../../IDHANMigration/src/table_bandwidth_hosts/446-create.sql" suffix(,)
		'\0'
	};
	static constexpr char convert_logs[] {
#embed "../../IDHANMigration/src/table_bandwidth_logs/447-host-ids.sql" suffix(,)
		'\0'
	};
	transaction.exec( create_logs );
	transaction.exec( "INSERT INTO bandwidth_logs (host, timestamp, bytes_received) VALUES "
		"('gelbooru.com', 100, 10), ('gelbooru.com', 100, 20), ('img4.gelbooru.com', 101, 40)" );
	transaction.exec( create_hosts );
	static constexpr char compact_logs[] {
#embed "../../IDHANMigration/src/table_bandwidth_logs/448-compact-seconds.sql" suffix(,)
		'\0'
	};
	transaction.exec( convert_logs );
	transaction.exec( compact_logs );
	CHECK( transaction.query_value< int >( "SELECT count(*) FROM bandwidth_hosts" ) == 2 );
	CHECK( transaction.query_value< int >( "SELECT count(*) FROM bandwidth_logs" ) == 2 );
	CHECK( transaction.query_value< int >( "SELECT sum(bytes_received) FROM bandwidth_logs "
		"JOIN bandwidth_hosts USING (host_id) WHERE host = 'gelbooru.com'" ) == 30 );
	CHECK( transaction.query_value< int >( "SELECT sum(bytes_received) FROM bandwidth_logs "
		"JOIN bandwidth_hosts USING (host_id) WHERE host = 'img4.gelbooru.com'" ) == 40 );
	CHECK( transaction.query_value< int >( "SELECT count(*) FROM information_schema.columns "
		"WHERE table_schema = 'bandwidth_host_migration_test' AND table_name = 'bandwidth_logs'" ) == 3 );
}

} // namespace idhan::test
