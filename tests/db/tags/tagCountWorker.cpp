#include <folly/executors/CPUThreadPoolExecutor.h>

#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>

#include "IDHANTypes.hpp"
#include "MappingHelpers.hpp"
#include "TagHelpers.hpp"
#include "db/tagCounts.hpp"
#include "idhan/db/DBContext.hpp"

// Load Catch assertions after Folly/glog headers, which also define CHECK.
#include <catch2/catch_test_macros.hpp>

#include "MigratedSchema.hpp"

namespace idhan::test
{
class TagCountWorkerSchema : public MigratedSchema
{
	folly::CPUThreadPoolExecutor m_executor { 1 };
	std::unique_ptr< db::DBContext > m_context {};

  public:

	TagCountWorkerSchema()
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

	~TagCountWorkerSchema() { run( m_context->drain() ); }

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

	pqxx::connection& sql() { return connection(); }

	db::DBContext& context() { return *m_context; }

	std::unique_ptr< pqxx::connection > otherConnection()
	{
		auto other { std::make_unique< pqxx::connection >( connectionString() ) };
		pqxx::nontransaction tx { *other };
		tx.exec( "SET search_path = " + db::makeSearchPath( schema() ) );
		tx.exec( "SET statement_timeout = '5s'" );
		return other;
	}

	void rollbackBarrier()
	{
		// A failed DBTransaction queues rollback before releasing its sole pool lease.
		auto barrier { run( context().transaction() ) };
		run( barrier.commit() );
	}
};

TEST_CASE_METHOD(
	TagCountWorkerSchema,
	"Count workers see committed deltas only and skip competing consumers",
	"[db][tags][counts][worker]" )
{
	TagDomainID domain {};
	TagID tag {};
	RecordID record {};
	{
		pqxx::work setup { sql() };
		domain = insertDomain( setup, "worker" );
		tag = insertTag( setup, "", "tag" );
		record = insertRecord( setup, 1 );
		setup.commit();
	}
	{
		pqxx::work writer { sql() };
		insertMapping( writer, record, tag, domain );
		CHECK( run( db::accumulateTagCountDeltas( context() ) ).deltas == 0 );
		writer.commit();
	}
	{
		pqxx::work competing { sql() };
		competing.exec( "SELECT pg_advisory_xact_lock('tag_count_delta'::regclass::oid::bigint)" );
		CHECK( run( db::accumulateTagCountDeltas( context() ) ).deltas == 0 );
		CHECK( competing.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 1 );
		CHECK( competing.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
	}
	const auto batch { run( db::accumulateTagCountDeltas( context() ) ) };
	CHECK( batch.deltas == 1 );
	CHECK( batch.counters == 1 );
	CHECK( run( db::accumulateTagCountDeltas( context() ) ).deltas == 0 );
	pqxx::nontransaction read { sql() };
	CHECK( read.query_value< int >( "SELECT storage_count FROM tag_counts" ) == 1 );
	CHECK( read.query_value< int >( "SELECT display_count FROM tag_counts" ) == 1 );
}

TEST_CASE_METHOD(
	TagCountWorkerSchema,
	"Failed count workers restore their queue and retry without double counting",
	"[db][tags][counts][worker]" )
{
	{
		pqxx::work setup { sql() };
		const auto domain { insertDomain( setup, "retry" ) };
		const auto source { insertTag( setup, "", "source" ) };
		const auto target { insertTag( setup, "", "target" ) };
		const auto record { insertRecord( setup, 1 ) };
		insertAlias( setup, domain, source, target );
		insertMapping( setup, record, source, domain );
		setup.exec(
			"CREATE FUNCTION reject_count() RETURNS trigger LANGUAGE plpgsql AS $$ "
			"BEGIN IF NEW.display_count <> 0 THEN RAISE EXCEPTION 'injected count failure'; END IF; "
			"RETURN NEW; END $$; "
			"CREATE TRIGGER reject_count BEFORE INSERT ON tag_counts "
			"FOR EACH ROW EXECUTE FUNCTION reject_count()" );
		setup.commit();
	}
	CHECK_THROWS( run( db::accumulateTagCountDeltas( context() ) ) );
	rollbackBarrier();
	{
		pqxx::nontransaction read { sql() };
		CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 2 );
		CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_counts" ) == 0 );
		CHECK( read.query_value< int >( "SELECT COUNT(*) FROM active_tag_mappings" ) == 1 );
		read.exec( "DROP TRIGGER reject_count ON tag_counts" );
	}
	CHECK( run( db::accumulateTagCountDeltas( context() ) ).deltas == 2 );
	CHECK( run( db::accumulateTagCountDeltas( context() ) ).deltas == 0 );
	pqxx::nontransaction read { sql() };
	CHECK( read.query_value< int >( "SELECT SUM(storage_count) FROM tag_counts" ) == 1 );
	CHECK( read.query_value< int >( "SELECT SUM(display_count) FROM tag_counts" ) == 1 );
}

TEST_CASE_METHOD(
	TagCountWorkerSchema,
	"Appending during count accumulation leaves new deltas for the next pass",
	"[db][tags][counts][worker]" )
{
	TagDomainID domain {};
	TagID tag {};
	RecordID second {}, third {};
	{
		pqxx::work setup { sql() };
		domain = insertDomain( setup, "concurrent appends" );
		tag = insertTag( setup, "", "tag" );
		insertMapping( setup, insertRecord( setup, 1 ), tag, domain );
		second = insertRecord( setup, 2 );
		third = insertRecord( setup, 3 );
		setup.commit();
	}
	run( db::accumulateTagCountDeltas( context() ) );
	{
		pqxx::work writer { sql() };
		insertMapping( writer, second, tag, domain );
		writer.commit();
	}
	auto other { otherConnection() };
	pqxx::work held { sql() };
	held.exec( "SELECT tag_id FROM tag_counts FOR UPDATE" );
	const auto blocker_pid { held.query_value< int >( "SELECT pg_backend_pid()" ) };
	auto in_flight { start( db::accumulateTagCountDeltas( context() ) ) };
	bool waiting {};
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 3 } };
	while ( !waiting && std::chrono::steady_clock::now() < deadline )
	{
		pqxx::nontransaction inspect { *other };
		waiting = inspect.query_value< bool >(
			"SELECT EXISTS (SELECT 1 FROM pg_stat_activity WHERE datname = current_database() "
			"AND wait_event_type = 'Lock' AND query LIKE '%WITH pending AS%' "
			"AND $1 = ANY(pg_blocking_pids(pid)))",
			pqxx::params { blocker_pid } );
		if ( !waiting ) std::this_thread::sleep_for( std::chrono::milliseconds { 10 } );
	}
	if ( waiting )
	{
		pqxx::work writer { *other };
		insertMapping( writer, third, tag, domain );
		writer.commit();
	}
	held.commit();
	const auto batch { std::move( in_flight ).get() };
	REQUIRE( waiting );
	CHECK( batch.deltas == 1 );
	{
		pqxx::nontransaction read { sql() };
		CHECK( read.query_value< int >( "SELECT COUNT(*) FROM tag_count_delta" ) == 1 );
		CHECK( read.query_value< int >( "SELECT display_count FROM tag_counts" ) == 2 );
	}
	CHECK( run( db::accumulateTagCountDeltas( context() ) ).deltas == 1 );
	pqxx::nontransaction read { sql() };
	CHECK( read.query_value< int >( "SELECT display_count FROM tag_counts" ) == 3 );
}
} // namespace idhan::test
