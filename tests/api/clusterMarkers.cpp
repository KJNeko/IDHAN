#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <catch2/catch_test_macros.hpp>
#include <json/reader.h>

#include <filesystem>
#include <fstream>

#include "ServerFixture.hpp"

namespace idhan::test
{

class ClusterMarkerFixture : public ServerFixture
{
  public:

	const std::string name { "marker-" + boost::uuids::to_string( boost::uuids::random_generator {}() ) };
	const std::filesystem::path directory { std::filesystem::temp_directory_path() / name };
	const std::filesystem::path root { directory / "root" };
	ClusterID cluster_id {};

	ClusterMarkerFixture() { std::filesystem::create_directories( root ); }

	~ClusterMarkerFixture()
	{
		if ( cluster_id != 0 )
		{
			try
			{
				(void)api().del( std::format( "/clusters/{}/remove", cluster_id ) );
			}
			catch ( const std::exception& error )
			{
				WARN( error.what() );
			}
		}
		std::error_code error;
		std::filesystem::remove_all( directory, error );
	}

	Json::Value body( const std::filesystem::path& path, const std::string& suffix = "", const bool readonly = false )
		const
	{
		Json::Value value { Json::objectValue };
		value[ "name" ] = name + suffix;
		value[ "path" ] = path.string();
		value[ "readonly" ] = readonly;
		return value;
	}

	void create( const bool readonly = false )
	{
		const auto response { api().post( "/clusters/add", body( root, "", readonly ) ) };
		REQUIRE( response.status == 201 );
		cluster_id = static_cast< ClusterID >( response.json[ "cluster_id" ].asInt() );
	}

	Json::Value marker() const
	{
		std::ifstream file { root / ".idhan-cluster.json" };
		Json::Value value;
		file >> value;
		return value;
	}

	void writeMarker( const Json::Value& document ) const
	{
		std::ofstream file { root / ".idhan-cluster.json" };
		file << document;
	}
};

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Cluster markers bind identity and preserve extensible metadata",
	"[api][cluster-markers]" )
{
	create( true );
	auto document { marker() };
	CHECK( document[ "format" ].asString() == "idhan.cluster" );
	CHECK( document[ "version" ].asInt() == 1 );
	CHECK( document[ "metadata" ].isObject() );
	{
		pqxx::work transaction { db() };
		CHECK(
			transaction.query_value< std::string >(
				"SELECT cluster_uuid::text FROM file_clusters WHERE cluster_id = $1", pqxx::params { cluster_id } )
			== document[ "cluster_uuid" ].asString() );
	}
	document[ "metadata" ][ "future" ] = "keep this";
	document[ "extra" ] = 123;
	writeMarker( document );
	Json::Value change { Json::objectValue };
	change[ "name" ] = name + "-renamed";
	CHECK( api().patch( std::format( "/clusters/{}/modify", cluster_id ), change ).status == 200 );
	CHECK( marker() == document );
	CHECK( api().del( std::format( "/clusters/{}/remove", cluster_id ) ).status == 200 );
	cluster_id = 0;
	CHECK( std::filesystem::is_regular_file( root / ".idhan-cluster.json" ) );
	create( true );
	CHECK( marker() == document );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Physical aliases and copied cluster markers conflict",
	"[api][cluster-markers]" )
{
	create();
	const auto original { marker() };
	const auto alias { directory / "alias" };
	std::filesystem::create_directory_symlink( root, alias );
	CHECK( api().post( "/clusters/add", body( alias, "-alias" ) ).status == 409 );
	const auto copied { directory / "copied" };
	std::filesystem::create_directory( copied );
	std::filesystem::copy_file( root / ".idhan-cluster.json", copied / ".idhan-cluster.json" );
	CHECK( api().post( "/clusters/add", body( copied, "-copy" ) ).status == 409 );
	CHECK( marker() == original );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Missing and mismatched markers block media access without detaching records",
	"[api][cluster-markers]" )
{
	create();
	const auto original { marker() };
	const std::string bytes { "IDHAN guarded cluster contents" };
	const auto imported {
		api().postOctets( "/file/import", bytes, { { "force_import", "true" }, { "filename", "marker.bin" } } )
	};
	REQUIRE( imported.status == 200 );
	REQUIRE( imported.json[ "status" ].asInt() == 1 );
	const auto record_id { imported.json[ "record_id" ].asInt() };
	const auto file_path { std::format( "/records/{}/file", record_id ) };
	CHECK( api().get( file_path ).body == bytes );
	std::filesystem::remove( root / ".idhan-cluster.json" );
	CHECK( api().get( file_path ).status == 503 );
	CHECK( api().postWithoutBody( std::format( "/clusters/{}/scan", cluster_id ) ).status == 503 );
	CHECK( api().postOctets( "/file/import", bytes, { { "force_import", "true" } } ).status == 503 );
	CHECK_FALSE( std::filesystem::exists( root / ".idhan-cluster.json" ) );
	{
		pqxx::work transaction { db() };
		CHECK(
			transaction.query_value< ClusterID >(
				"SELECT cluster_id FROM file_info WHERE record_id = $1", pqxx::params { record_id } )
			== cluster_id );
	}
	auto wrong { original };
	wrong[ "cluster_uuid" ] = boost::uuids::to_string( boost::uuids::random_generator {}() );
	writeMarker( wrong );
	CHECK( api().get( file_path ).status == 503 );
	CHECK( api().postWithoutBody( std::format( "/clusters/{}/scan", cluster_id ) ).status == 503 );
	CHECK( marker() == wrong );
	writeMarker( original );
	CHECK( api().get( file_path ).body == bytes );
}

} // namespace idhan::test
