#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <format>
#include <string>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

static std::string hashOf( pqxx::connection& connection, const RecordID record_id )
{
	pqxx::work tx { connection };
	return tx.query_value< std::string >(
		"SELECT encode(sha256, 'hex') FROM records WHERE record_id = $1", pqxx::params { record_id } );
}

//! Writes \p contents where the server expects the record's file to be, and registers the cluster
//! holding it. The layout is the one getRecordPath builds: <cluster>/f<first two hex>/<hex>.<ext>.
static void storeFile(
	pqxx::connection& connection,
	const RecordID record_id,
	const std::string& hex,
	const std::filesystem::path& cluster,
	const std::string& contents )
{
	const auto folder { cluster / std::format( "f{}", hex.substr( 0, 2 ) ) };
	std::filesystem::create_directories( folder );

	{
		std::ofstream file { folder / std::format( "{}.png", hex ), std::ios::binary };
		file << contents;
	}

	pqxx::work tx { connection };
	const auto cluster_id { tx.query_value< int >(
		"INSERT INTO file_clusters (folder_path) VALUES ($1) RETURNING cluster_id",
		pqxx::params { cluster.string() } ) };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_id, cluster_store_time) "
		"VALUES ($1, $2, $3, $4, now())",
		pqxx::params { record_id, contents.size(), mime_ids::IMAGE_PNG, cluster_id } );
	tx.commit();
}

SCENARIO_METHOD( ServerFixture, "A fetched file is named after its hash", "[api][file]" )
{
	const auto record_id { api().createRecord( 501 ) };
	const auto hex { hashOf( db(), record_id ) };
	const std::string contents { "not really a png, but the bytes do not matter here" };

	const auto cluster { std::filesystem::temp_directory_path() / std::format( "idhan-file-test-{}", record_id ) };
	std::filesystem::remove_all( cluster );
	storeFile( db(), record_id, hex, cluster, contents );

	const auto response { api().get( std::format( "/records/{}/file", record_id ) ) };

	CHECK( response.status == drogon::k200OK );
	CHECK( response.body == contents );
	// Without this a browser saves every record in the collection as "file", after the URL's last segment.
	CHECK( response.header( "content-disposition" ) == std::format( "inline; filename=\"{}.png\"", hex ) );

	std::filesystem::remove_all( cluster );
}

} // namespace idhan::test
