#include <catch2/catch_test_macros.hpp>

#include <format>

#include "IDHANTypes.hpp"
#include "MigratedSchema.hpp"
#include "MimeIDs.hpp"

namespace idhan::test
{

class MissingFilesSchema : public MigratedSchema
{
  public:

	pqxx::connection& db() { return connection(); }

	ClusterID makeCluster( pqxx::transaction_base& tx )
	{
		return tx.query_value< ClusterID >(
			"INSERT INTO file_clusters (folder_path)\n"
			"    VALUES ($1)\n"
			"RETURNING cluster_id",
			pqxx::params { std::format( "/missing-files-test-{}", currentTestName() ) } );
	}

	RecordID makeStoredRecord( pqxx::transaction_base& tx, const ClusterID cluster_id )
	{
		const auto record_id { tx.query_value< RecordID >(
				"INSERT INTO records (sha256)\n"
				"    VALUES (decode(repeat('01', 32), 'hex'))\n"
				"RETURNING record_id" )
		};
		tx.exec(
			"INSERT INTO file_info (record_id, size, mime_id, cluster_id)\n"
			"    VALUES ($1, 1, $2, $3)",
			pqxx::params { record_id, mime_ids::IMAGE_PNG, cluster_id } );
		return record_id;
	}
};

TEST_CASE_METHOD( MissingFilesSchema, "A missing file has no cluster and no delete time", "[db][missing-files]" )
{
	pqxx::work tx { db() };
	const auto cluster_id { makeCluster( tx ) };
	const auto record_id { makeStoredRecord( tx, cluster_id ) };

	tx.exec( "UPDATE\n"
			 "    file_info\n"
			 "SET cluster_id = NULL\n"
			 "WHERE record_id = $1",
			 pqxx::params{ record_id } );

	const auto missing { tx.exec(
			"SELECT\n"
			"    cluster_id,\n"
			"    cluster_delete_time\n"
			"FROM file_info\n"
			"WHERE record_id = $1",
			pqxx::params{ record_id } )
	};
	REQUIRE( missing.size() == 1 );
	CHECK( missing[ 0 ][ "cluster_id" ].is_null() );
	CHECK( missing[ 0 ][ "cluster_delete_time" ].is_null() );

	CHECK( tx.query_value< bool >(
		"SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM missing_files\n"
		"        WHERE record_id = $1\n"
		"    )",
		pqxx::params{ record_id } ) );

	tx.exec( "UPDATE\n"
			 "    file_info\n"
			 "SET cluster_id = $2\n"
			 "WHERE record_id = $1",
			 pqxx::params{ record_id, cluster_id } );

	CHECK_FALSE( tx.query_value< bool >(
		"SELECT\n"
		"    EXISTS (\n"
		"        SELECT\n"
		"            1\n"
		"        FROM missing_files\n"
		"        WHERE record_id = $1\n"
		"    )",
		pqxx::params{ record_id } ) );
}

TEST_CASE_METHOD( MissingFilesSchema, "A file cannot be stored and deleted at once", "[db][missing-files]" )
{
	pqxx::work tx { db() };
	const auto cluster_id { makeCluster( tx ) };
	const auto record_id { makeStoredRecord( tx, cluster_id ) };

	CHECK_THROWS( tx.exec(
		"UPDATE\n"
		"    file_info\n"
		"SET cluster_delete_time = now()\n"
		"WHERE record_id = $1",
		pqxx::params{ record_id } ) );
}

} // namespace idhan::test
