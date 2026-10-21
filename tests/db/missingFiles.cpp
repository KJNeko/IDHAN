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
			"INSERT INTO file_clusters (folder_path) VALUES ($1) RETURNING cluster_id",
			pqxx::params { std::format( "/missing-files-test-{}", currentTestName() ) } );
	}

	RecordID makeStoredRecord( pqxx::transaction_base& tx, const ClusterID cluster_id )
	{
		const auto record_id { tx.query_value< RecordID >(
			"INSERT INTO records (sha256) VALUES (decode(repeat('01', 32), 'hex')) RETURNING record_id" ) };
		tx.exec(
			"INSERT INTO file_info (record_id, size, mime_id, cluster_id) VALUES ($1, 1, $2, $3)",
			pqxx::params { record_id, mime_ids::IMAGE_PNG, cluster_id } );
		return record_id;
	}
};

TEST_CASE_METHOD( MissingFilesSchema, "A missing file has no cluster and no delete time", "[db][missing-files]" )
{
	pqxx::work tx { db() };
	const auto cluster_id { makeCluster( tx ) };
	const auto record_id { makeStoredRecord( tx, cluster_id ) };

	tx.exec( "UPDATE file_info SET cluster_id = NULL WHERE record_id = $1", pqxx::params { record_id } );

	const auto missing { tx.exec(
		"SELECT cluster_id, cluster_delete_time FROM file_info WHERE record_id = $1", pqxx::params { record_id } ) };
	REQUIRE( missing.size() == 1 );
	CHECK( missing[ 0 ][ "cluster_id" ].is_null() );
	CHECK( missing[ 0 ][ "cluster_delete_time" ].is_null() );

	CHECK( tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM missing_files WHERE record_id = $1)", pqxx::params { record_id } ) );

	tx.exec( "UPDATE file_info SET cluster_id = $2 WHERE record_id = $1", pqxx::params { record_id, cluster_id } );

	CHECK_FALSE( tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM missing_files WHERE record_id = $1)", pqxx::params { record_id } ) );
}

TEST_CASE_METHOD( MissingFilesSchema, "A file cannot be stored and deleted at once", "[db][missing-files]" )
{
	pqxx::work tx { db() };
	const auto cluster_id { makeCluster( tx ) };
	const auto record_id { makeStoredRecord( tx, cluster_id ) };

	CHECK_THROWS( tx.exec(
		"UPDATE file_info SET cluster_delete_time = now() WHERE record_id = $1", pqxx::params { record_id } ) );
}

} // namespace idhan::test
