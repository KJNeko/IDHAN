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
};

TEST_CASE_METHOD( MissingFilesSchema, "Saving a missing file clears its marker", "[db][missing-files]" )
{
	pqxx::work tx { db() };
	const auto cluster_id { tx.query_value< ClusterID >(
		"INSERT INTO file_clusters (folder_path) VALUES ($1) RETURNING cluster_id",
		pqxx::params { std::format( "/missing-files-test-{}", currentTestName() ) } ) };
	const auto record_id { tx.query_value< RecordID >(
		"INSERT INTO records (sha256) VALUES (decode(repeat('01', 32), 'hex')) RETURNING record_id" ) };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_id) VALUES ($1, 1, $2, $3)",
		pqxx::params { record_id, mime_ids::IMAGE_PNG, cluster_id } );
	tx.exec( "INSERT INTO missing_files (record_id) VALUES ($1)", pqxx::params { record_id } );

	CHECK( tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM missing_files WHERE record_id = $1)", pqxx::params { record_id } ) );

	tx.exec(
		"UPDATE file_info SET cluster_store_time = now() + interval '1 second' WHERE record_id = $1",
		pqxx::params { record_id } );

	CHECK_FALSE( tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM missing_files WHERE record_id = $1)", pqxx::params { record_id } ) );
}

} // namespace idhan::test
