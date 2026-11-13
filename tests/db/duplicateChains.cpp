#include <catch2/catch_test_macros.hpp>

#include <array>
#include <fstream>
#include <iterator>

#include "MigratedSchema.hpp"

namespace idhan::test
{

class DuplicateMembershipSchema : public MigratedSchema
{
  public:

	pqxx::connection& db() { return connection(); }

	const std::string& schemaName() const { return schema(); }
};

TEST_CASE_METHOD(
	DuplicateMembershipSchema,
	"Duplicate memberships stay flat and requeue inherited rulings",
	"[db][relationships]" )
{
	pqxx::work tx { db() };
	tx.exec(
		"INSERT INTO records (sha256)\n"
		"SELECT\n"
		"    decode(lpad(to_hex(seed), 64, '0'), 'hex')\n"
		"FROM generate_series(1, 8) AS seed" );
	std::ifstream source { IDHAN_SOURCE_DIR "/tests/db/duplicateChains.sql" };
	REQUIRE( source.is_open() );
	const std::string sql { std::istreambuf_iterator< char > { source }, std::istreambuf_iterator< char > {} };
	REQUIRE_NOTHROW( tx.exec( sql ) );
}

TEST_CASE_METHOD(
	DuplicateMembershipSchema,
	"Hash reset preserves relationships and waits for regenerated metadata",
	"[db][relationships][phash]" )
{
	pqxx::work tx { db() };
	const auto a { tx.query_value< RecordID >(
			"INSERT INTO records (sha256)\n"
			"    VALUES (decode(repeat('01', 32), 'hex'))\n"
			"RETURNING record_id" )
	};
	const auto b { tx.query_value< RecordID >(
			"INSERT INTO records (sha256)\n"
			"    VALUES (decode(repeat('02', 32), 'hex'))\n"
			"RETURNING record_id" )
	};
	tx.exec( "SELECT\n"
			 "    insert_duplicate_pair ($1, $2)",
			 pqxx::params{ b, a } );
	tx.exec(
		"INSERT INTO image_metadata (record_id, width, height, channels, phash)\n"
		"VALUES\n"
		"    ($1, 1, 1, 3, 'xb44dc7b24dcb381c'),\n"
		"    ($2, 1, 1, 3, 'xb44dc7b24dcb381d')",
		pqxx::params { a, b } );
	tx.exec( "INSERT INTO hamming_distance\n"
			 "    VALUES ($1, $2, 1)",
			 pqxx::params{ a, b } );

	std::ifstream source { IDHAN_SOURCE_DIR "/IDHANMigration/src/table_hamming_distance/396-reset-shape-hashes.sql" };
	REQUIRE( source.is_open() );
	const std::string sql { std::istreambuf_iterator< char > { source }, std::istreambuf_iterator< char > {} };
	tx.exec( sql );
	CHECK( tx.query_value< Integer >( "SELECT\n"
			   "    count(*)\n"
			   "FROM image_metadata\n"
			   "WHERE phash IS NOT NULL" ) == 0 );
	CHECK( tx.query_value< Integer >( "SELECT\n"
			   "    count(*)\n"
			   "FROM hamming_distance" ) == 0 );
	CHECK( tx.query_value< Integer >( "SELECT\n"
			   "    count(*)\n"
			   "FROM hamming_distance_queue" ) == 0 );
	CHECK( tx.query_value< RecordID >( "SELECT\n"
									   "    duplicate_king ($1)",
									   pqxx::params{ b } ) == a );

	tx.exec( "UPDATE\n"
			 "    image_metadata\n"
			 "SET phash = 'xb44dc7b24dcb381c'\n"
			 "WHERE record_id = $1",
			 pqxx::params{ a } );
	CHECK( tx.query_value< RecordID >( "SELECT\n"
			   "    record_id\n"
			   "FROM hamming_distance_queue" ) == a );
}

TEST_CASE_METHOD(
	DuplicateMembershipSchema,
	"Relationship writes wait for an in-flight distance sweep",
	"[db][relationships]" )
{
	RecordID a {};
	RecordID b {};
	{
		pqxx::work seed { db() };
		a = seed.query_value< RecordID >(
			"INSERT INTO records (sha256)\n"
			"    VALUES (decode(repeat('01', 32), 'hex'))\n"
			"RETURNING record_id" );
		b = seed.query_value< RecordID >(
			"INSERT INTO records (sha256)\n"
			"    VALUES (decode(repeat('02', 32), 'hex'))\n"
			"RETURNING record_id" );
		seed.commit();
	}

	pqxx::connection writer { connectionString() };
	pqxx::nontransaction write { writer };
	write.exec( "SET search_path = " + db::makeSearchPath( schemaName() ) );
	write.exec( "SET lock_timeout = '100ms'" );

	// Hold the sweeper's transaction lock while a second connection attempts each ruling type.
	pqxx::work sweep { db() };
	sweep.exec( "SELECT\n"
		"    pg_advisory_xact_lock(16609, 1)" );
	const std::array< const char*, 6 > statements {
		{
			"INSERT INTO alternative_records\n"
			"    VALUES ($1, $2)",
			"INSERT INTO unrelated_records\n"
			"    VALUES ($1, $2)",
			"DELETE FROM alternative_records\n"
			"WHERE lesser_record_id = $1\n"
			"    AND greater_record_id = $2",
			"DELETE FROM unrelated_records\n"
			"WHERE lesser_record_id = $1\n"
			"    AND greater_record_id = $2",
			"SELECT\n"
			"    insert_duplicate_pair ($1, $2)",
			"SELECT\n"
			"    remove_duplicate_pair ($1, $2)"
		}
	};
	for ( const auto* statement : statements )
	{
		INFO( statement );
		try
		{
			write.exec( statement, pqxx::params { a, b } );
			FAIL( "The ruling did not wait for the sweep" );
		}
		catch ( const pqxx::sql_error& error )
		{
			CHECK( error.sqlstate() == "55P03" );
		}
	}

	// After the sweep commits, the waiting ruling must delete its distance.
	sweep.exec( "INSERT INTO hamming_distance\n"
				"    VALUES ($1, $2, 0)",
				pqxx::params{ a, b } );
	sweep.commit();
	write.exec( "INSERT INTO alternative_records\n"
				"    VALUES ($1, $2)",
				pqxx::params{ a, b } );
	CHECK( write.query_value< Integer >( "SELECT\n"
			   "    count(*)\n"
			   "FROM hamming_distance" ) == 0 );
}

} // namespace idhan::test
