#include <catch2/catch_test_macros.hpp>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <chrono>
#include <future>
#include <thread>
#include <type_traits>

#include "MigratedSchema.hpp"
#include "db/hashMaintenance.hpp"
#include "hamming/hamming.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::test
{

static void replaceHash( pqxx::connection& connection, const RecordID id )
{
	pqxx::work transaction { connection };
	transaction.exec(
		"UPDATE\n"
		"    image_metadata\n"
		"SET phash = 'xffffffffffffffff'\n"
		"WHERE record_id = $1",
		pqxx::params{ id } );
	transaction.commit();
}

class HashMaintenanceSchema : public MigratedSchema
{
	folly::CPUThreadPoolExecutor m_executor { 1 };
	std::unique_ptr< db::DBContext > m_context {};

  public:

	HashMaintenanceSchema()
	{
		const PostgresSettings settings;
		db::DBConfig config;
		config.host = settings.host;
		config.port = static_cast< std::uint16_t >( std::stoul( settings.port ) );
		config.database = settings.database;
		config.user = settings.user;
		config.password = settings.password;
		config.schema = schema();
		config.connections = 2;
		config.transaction_connections = 1;
		config.worker_threads = 2;
		config.statement_timeout = std::chrono::seconds { 5 };
		m_context = std::make_unique< db::DBContext >( config, folly::getKeepAliveToken( &m_executor ) );
		run( m_context->start() );
	}

	~HashMaintenanceSchema() { run( m_context->drain() ); }

	template < typename T >
	T run( runtime::Task< T > task )
	{
		auto future { start( std::move( task ) ) };
		if constexpr ( std::is_void_v< T > )
			std::move( future ).get();
		else
			return std::move( future ).get();
	}

	template < typename T >
	auto start( runtime::Task< T > task )
	{
		return folly::coro::co_withExecutor( folly::getKeepAliveToken( &m_executor ), std::move( task ) ).start();
	}

	db::DBContext& context() { return *m_context; }

	pqxx::connection& sql() { return connection(); }

	void execute( const std::string& statement )
	{
		pqxx::nontransaction transaction { sql() };
		transaction.exec( statement );
	}

	Integer count( const std::string& table )
	{
		pqxx::nontransaction transaction { sql() };
		return transaction.query_value< Integer >( "SELECT\n"
												   "    count(*)\n"
												   "FROM " + transaction.quote_name( table ) );
	}

	RecordID image( const int seed, const bool hashed = true )
	{
		pqxx::work transaction { sql() };
		const auto id { transaction.query_value< RecordID >(
			"INSERT INTO records (sha256)\n"
			"    VALUES (decode(lpad(to_hex($1::integer), 64, '0'), 'hex'))\n"
			"RETURNING record_id",
			pqxx::params { seed } ) };
		transaction.exec(
			"INSERT INTO image_metadata (record_id, width, height, channels, phash)\n"
			"    VALUES ($1, 123, 456, 3, CASE WHEN $2 THEN 'x0000000000000000'::bit(64)\n"
			"        ELSE\n"
			"            NULL\n"
			"        END)",
			pqxx::params { id, hashed } );
		transaction.commit();
		return id;
	}

	ClusterID cluster( const std::string& path )
	{
		pqxx::nontransaction transaction { sql() };
		return transaction.query_value< ClusterID >(
			"INSERT INTO file_clusters (folder_path)\n"
			"    VALUES ($1)\n"
			"RETURNING cluster_id",
			pqxx::params{ path } );
	}

	void assign( const RecordID id, const ClusterID cluster_id )
	{
		pqxx::nontransaction transaction { sql() };
		transaction.exec(
			"INSERT INTO file_info (record_id, mime_id, cluster_id, size)\n"
			"    VALUES ($1, $2, $3, 987)",
			pqxx::params { id, mime_ids::IMAGE_PNG, cluster_id } );
	}

	std::unique_ptr< pqxx::connection > otherConnection()
	{
		auto connection { std::make_unique< pqxx::connection >( connectionString() ) };
		pqxx::nontransaction transaction { *connection };
		transaction.exec( "SET search_path = " + db::makeSearchPath( schema() ) );
		transaction.exec( "SET statement_timeout = '5s'" );
		return connection;
	}
};

TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"Distance batches are bounded and skip locked queue entries",
	"[maintenance][phash]" )
{
	RecordID first {};
	RecordID last {};
	for ( int seed { 1 }; seed <= 35; ++seed )
	{
		last = image( seed );
		if ( seed == 1 ) first = last;
	}
	const auto through { run( db::queuedHammingHighWater( context() ) ) };
	REQUIRE( through == last );
	auto locker { otherConnection() };
	pqxx::work held { *locker };
	held.exec( "SELECT\n"
			   "    record_id\n"
			   "FROM hamming_distance_queue\n"
			   "WHERE record_id = $1\n"
			   "FOR UPDATE",
			   pqxx::params{ first } );
	auto index { run( db::loadPhashIndex( context() ) ) };
	REQUIRE( index.size() == 35 );
	const auto batch { run( hamming::processQueueBatch( context(), index, *through ) ) };
	REQUIRE( batch.has_value() );
	CHECK( batch->processed_records == 32 );
	REQUIRE( count( "hamming_distance_queue" ) == 3 );
	held.abort();
	CHECK( run( hamming::runQueueSweep( context() ) ) == 3 );
	REQUIRE( count( "hamming_distance_queue" ) == 0 );
	REQUIRE( count( "hamming_distance" ) == 35 * 34 / 2 );
}

TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"A batch compares claimed records by their current hash, not the loaded one",
	"[maintenance][phash]" )
{
	const auto a { image( 1 ) };
	image( 2 );
	run( hamming::runQueueSweep( context() ) );
	REQUIRE( count( "hamming_distance" ) == 1 );
	auto index { run( db::loadPhashIndex( context() ) ) };
	execute( "UPDATE\n"
			 "    image_metadata\n"
			 "SET phash = 'xffffffffffffffff'\n"
			 "WHERE record_id = " + std::to_string( a ) );
	const auto through { run( db::queuedHammingHighWater( context() ) ) };
	REQUIRE( through == a );
	const auto batch { run( hamming::processQueueBatch( context(), index, *through ) ) };
	REQUIRE( batch.has_value() );
	CHECK( index.find( a ) == 0xFFFF'FFFF'FFFF'FFFFull );
	REQUIRE( count( "hamming_distance" ) == 0 );
}

TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"A failed distance rebuild restores the queue and old pairs",
	"[maintenance][phash]" )
{
	image( 1 );
	image( 2 );
	run( hamming::runQueueSweep( context() ) );
	REQUIRE( count( "hamming_distance" ) == 1 );
	execute( "UPDATE\n"
		"    image_metadata\n"
		"SET phash = 'x0000000000000001'" );
	execute(
		"CREATE FUNCTION fail_distance_write ()\n"
		"    RETURNS trigger\n"
		"    LANGUAGE plpgsql\n"
		"    AS $$\n"
		"BEGIN\n"
		"    RAISE EXCEPTION 'injected failure';\n"
		"END\n"
		"$$;\n"
		"\n"
		"CREATE TRIGGER fail_distance_write\n"
		"    BEFORE INSERT ON hamming_distance\n"
		"    FOR EACH ROW\n"
		"    EXECUTE FUNCTION fail_distance_write ()" );
	CHECK_THROWS( run( hamming::runQueueSweep( context() ) ) );
	// Acquiring the sole transaction lease also waits for the failed batch's rollback.
	auto barrier { run( context().transaction() ) };
	run( barrier.commit() );
	REQUIRE( count( "hamming_distance_queue" ) == 2 );
	REQUIRE( count( "hamming_distance" ) == 1 );
	execute( "DROP TRIGGER fail_distance_write ON hamming_distance" );
	run( hamming::runQueueSweep( context() ) );
	REQUIRE( count( "hamming_distance_queue" ) == 0 );
	REQUIRE( count( "hamming_distance" ) == 1 );
}

TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"Distance rebuilding respects flattened groups and their rulings",
	"[maintenance][phash]" )
{
	const auto a { image( 1 ) };
	const auto b { image( 2 ) };
	const auto c { image( 3 ) };
	const auto d { image( 4 ) };
	{
		pqxx::nontransaction transaction { sql() };
		transaction.exec( "SELECT\n"
						  "    insert_duplicate_pair ($1::integer, $2::integer)",
						  pqxx::params{ b, a } );
		transaction.exec( "INSERT INTO alternative_records\n"
						  "    VALUES ($1, $2)",
						  pqxx::params{ a, c } );
		transaction.exec( "INSERT INTO unrelated_records\n"
						  "    VALUES ($1, $2)",
						  pqxx::params{ a, d } );
	}
	run( hamming::runQueueSweep( context() ) );
	REQUIRE( count( "hamming_distance" ) == 1 );
	{
		pqxx::nontransaction transaction { sql() };
		REQUIRE( transaction.query_value< RecordID >( "SELECT\n"
					 "    left_id\n"
					 "FROM hamming_distance" ) == c );
		REQUIRE( transaction.query_value< RecordID >( "SELECT\n"
					 "    right_id\n"
					 "FROM hamming_distance" ) == d );
		transaction.exec( "DELETE FROM alternative_records" );
		transaction.exec( "DELETE FROM unrelated_records" );
	}
	run( hamming::runQueueSweep( context() ) );
	REQUIRE( count( "hamming_distance" ) == 5 );
}

TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"A hash changed during queue deletion remains queued after commit",
	"[maintenance][phash][concurrency]" )
{
	const auto a { image( 1 ) };
	image( 2 );
	// Block pair insertion after the worker has deleted (and locked) the claimed queue rows.
	execute(
		"CREATE FUNCTION pause_distance_write ()\n"
		"    RETURNS trigger\n"
		"    LANGUAGE plpgsql\n"
		"    AS $$\n"
		"BEGIN\n"
		"    PERFORM\n"
		"        pg_advisory_xact_lock(16609, 991);\n"
		"    RETURN NEW;\n"
		"END\n"
		"$$;\n"
		"\n"
		"CREATE TRIGGER pause_distance_write\n"
		"    BEFORE INSERT ON hamming_distance\n"
		"    FOR EACH ROW\n"
		"    EXECUTE FUNCTION pause_distance_write ()" );
	auto gate_connection { otherConnection() };
	pqxx::work gate { *gate_connection };
	gate.exec( "SELECT\n"
		"    pg_advisory_xact_lock(16609, 991)" );
	auto sweep { start( hamming::runQueueSweep( context() ) ) };
	bool paused {};
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 3 } };
	while ( !paused && std::chrono::steady_clock::now() < deadline )
	{
		pqxx::nontransaction transaction { sql() };
		paused = transaction.query_value< bool >( "SELECT\n"
			"    EXISTS (\n"
			"        SELECT\n"
			"            1\n"
			"        FROM pg_locks\n"
			"        WHERE locktype = 'advisory'\n"
			"            AND classid = 16609\n"
			"            AND objid = 991\n"
			"            AND NOT granted\n"
			"    )" );
		if ( !paused ) std::this_thread::sleep_for( std::chrono::milliseconds { 10 } );
	}
	REQUIRE( paused );
	auto writer { otherConnection() };
	// The trigger's reinsert must wait for the deleted queue row's transaction.
	const auto writer_pid { writer->backendpid() };
	auto update { std::async( std::launch::async, replaceHash, std::ref( *writer ), a ) };
	bool writer_waiting {};
	const auto writer_deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 3 } };
	while ( !writer_waiting && std::chrono::steady_clock::now() < writer_deadline )
	{
		pqxx::nontransaction transaction { sql() };
		writer_waiting = transaction.query_value< bool >(
			"SELECT\n"
			"    EXISTS (\n"
			"        SELECT\n"
			"            1\n"
			"        FROM pg_locks\n"
			"        WHERE pid = $1\n"
			"            AND NOT granted\n"
			"    )",
			pqxx::params{ writer_pid } );
		if ( !writer_waiting ) std::this_thread::sleep_for( std::chrono::milliseconds { 10 } );
	}
	REQUIRE( writer_waiting );
	REQUIRE( update.wait_for( std::chrono::milliseconds { 100 } ) == std::future_status::timeout );
	gate.commit();
	std::move( sweep ).get();
	update.get();
	REQUIRE( count( "hamming_distance_queue" ) == 1 );
	REQUIRE( count( "hamming_distance" ) == 1 );
	execute( "DROP TRIGGER pause_distance_write ON hamming_distance" );
	run( hamming::runQueueSweep( context() ) );
	REQUIRE( count( "hamming_distance_queue" ) == 0 );
	REQUIRE( count( "hamming_distance" ) == 0 );
}

TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"Backfill writes only a still-missing hash on the selected active file",
	"[maintenance][phash]" )
{
	const auto id { image( 1, false ) };
	const auto active { cluster( "/unused-test-cluster" ) };
	const auto relocated { cluster( "/unused-other-cluster" ) };
	assign( id, active );
	const auto through { run( db::missingPhashHighWater( context(), { mime_ids::IMAGE_PNG } ) ) };
	REQUIRE( through == id );
	const auto selected { run( db::missingPhashBatch( context(), {}, *through, 8, { mime_ids::IMAGE_PNG } ) ) };
	REQUIRE( selected.size() == 1 );
	const PerceptualHash computed { std::byte { 0x80 }, {}, {}, {}, {}, {}, {}, std::byte { 1 } };
	SECTION( "a foreground hash wins" )
	{
		execute( "UPDATE\n"
			"    image_metadata\n"
			"SET phash = 'xffffffffffffffff'" );
		CHECK_FALSE( run( db::updateMissingPhash( context(), selected.front(), computed ) ) );
		pqxx::nontransaction transaction { sql() };
		REQUIRE(
			transaction.query_value< std::string >( "SELECT\n"
				"    phash::text\n"
				"FROM image_metadata" )
			== std::string( 64, '1' ) );
	}
	SECTION( "a relocated file is not updated from its previous cluster" )
	{
		execute( "UPDATE\n"
				 "    file_info\n"
				 "SET cluster_id = " + std::to_string( relocated ) );
		CHECK_FALSE( run( db::updateMissingPhash( context(), selected.front(), computed ) ) );
	}
	SECTION( "a deleted file is excluded" )
	{
		execute( "UPDATE\n"
			"    file_info\n"
			"SET cluster_delete_time = now()" );
		CHECK_FALSE( run( db::updateMissingPhash( context(), selected.front(), computed ) ) );
		CHECK_FALSE( run( db::missingPhashHighWater( context(), { mime_ids::IMAGE_PNG } ) ) );
	}
	SECTION( "a changed MIME invalidates the parse" )
	{
		execute( "UPDATE\n"
				 "    file_info\n"
				 "SET mime_id = " + std::to_string( mime_ids::IMAGE_JPEG ) );
		CHECK_FALSE( run( db::updateMissingPhash( context(), selected.front(), computed ) ) );
	}
	SECTION( "successful backfill preserves other fields and queues distance maintenance" )
	{
		REQUIRE( run( db::updateMissingPhash( context(), selected.front(), computed ) ) );
		REQUIRE( count( "hamming_distance_queue" ) == 1 );
		pqxx::nontransaction transaction { sql() };
		REQUIRE(
			transaction.query_value< std::string >( "SELECT\n"
				"    phash::text\n"
				"FROM image_metadata" )
			== "1000000000000000000000000000000000000000000000000000000000000001" );
		REQUIRE( transaction.query_value< Integer >( "SELECT\n"
					 "    width\n"
					 "FROM image_metadata" ) == 123 );
		REQUIRE( transaction.query_value< Integer >( "SELECT\n"
					 "    height\n"
					 "FROM image_metadata" ) == 456 );
		REQUIRE( transaction.query_value< Integer >( "SELECT\n"
					 "    size\n"
					 "FROM file_info" ) == 987 );
	}
}

} // namespace idhan::test

namespace idhan::test
{
TEST_CASE_METHOD(
	HashMaintenanceSchema,
	"Backfill selects only missing hashes supported by loaded parsers",
	"[maintenance][phash][capabilities]" )
{
	const auto active { cluster( "/unused-capability-test-cluster" ) };
	const auto png { image( 1, false ) };
	const auto gif { image( 2, false ) };
	const auto unknown { image( 3, false ) };
	assign( png, active );
	assign( gif, active );
	assign( unknown, active );
	execute(
		"UPDATE\n"
		"    file_info\n"
		"SET mime_id = " + std::to_string( mime_ids::ANIMATION_GIF )
		+ " WHERE record_id = " + std::to_string( gif ) );
	execute(
		"UPDATE\n"
		"    file_info\n"
		"SET mime_id = " + std::to_string( mime_ids::APPLICATION_PSD )
		+ " WHERE record_id = " + std::to_string( unknown ) );

	CHECK_FALSE( run( db::missingPhashHighWater( context(), {} ) ) );
	CHECK( run( db::missingPhashBatch( context(), {}, unknown, 8, {} ) ).empty() );
	const auto through { run( db::missingPhashHighWater( context(), { mime_ids::IMAGE_PNG } ) ) };
	REQUIRE( through == png );
	const auto batch { run( db::missingPhashBatch( context(), {}, unknown, 1, { mime_ids::IMAGE_PNG } ) ) };
	REQUIRE( batch.size() == 1 );
	CHECK( batch.front().record_id == png );
	CHECK( run( db::missingPhashBatch( context(), png, unknown, 1, { mime_ids::IMAGE_PNG } ) ).empty() );

	// A newly installed parser makes previously unsupported records eligible without a migration.
	const auto expanded {
		run( db::missingPhashBatch( context(), png, unknown, 1, { mime_ids::IMAGE_PNG, mime_ids::ANIMATION_GIF } ) )
	};
	REQUIRE( expanded.size() == 1 );
	CHECK( expanded.front().record_id == gif );
}
} // namespace idhan::test
