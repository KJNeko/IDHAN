#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/File.h>
#include <json/reader.h>
#include <json/writer.h>

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>

#include "Config.hpp"
#include "config/consumeFlag.hpp"
#include "filesystem/clusters/ClusterAccess.hpp"
#include "filesystem/clusters/ClusterMarker.hpp"

#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::test
{

class MarkerDirectory
{
  public:

	const std::filesystem::path path {
		std::filesystem::temp_directory_path()
		/ ( "idhan-marker-" + boost::uuids::to_string( boost::uuids::random_generator {}() ) )
	};

	MarkerDirectory() { std::filesystem::create_directory( path ); }

	~MarkerDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all( path, error );
	}
};

static std::string markerContents( const std::filesystem::path& path )
{
	std::ifstream file { path };
	return { std::istreambuf_iterator< char > { file }, std::istreambuf_iterator< char > {} };
}

TEST_CASE( "Cluster markers are atomic, extensible, and never replaced", "[cluster-markers]" )
{
	MarkerDirectory directory;
	folly::File root { directory.path.c_str(), O_RDONLY | O_DIRECTORY };
	const auto uuid { boost::uuids::random_generator {}() };
	const auto created { filesystem::initializeClusterMarker( root.fd(), uuid ) };
	REQUIRE( created );
	CHECK( created->uuid == uuid );
	CHECK( created->document[ "metadata" ].isObject() );
	const auto marker_path { directory.path / filesystem::clusterMarkerFilename };
	Json::Value document { created->document };
	document[ "metadata" ][ "future" ] = "retain me";
	document[ "future_root" ] = 42;
	{
		std::ofstream marker { marker_path };
		marker << document;
	}
	const auto original { markerContents( marker_path ) };
	const auto again { filesystem::initializeClusterMarker( root.fd(), boost::uuids::random_generator {}() ) };
	REQUIRE( again );
	CHECK( again->uuid == uuid );
	CHECK( again->document[ "metadata" ][ "future" ].asString() == "retain me" );
	CHECK( again->document[ "future_root" ].asInt() == 42 );
	CHECK( markerContents( marker_path ) == original );
}

TEST_CASE( "Invalid cluster markers and symlinks are rejected without overwriting", "[cluster-markers]" )
{
	MarkerDirectory directory;
	folly::File root { directory.path.c_str(), O_RDONLY | O_DIRECTORY };
	const auto uuid { boost::uuids::random_generator {}() };
	const auto created { filesystem::initializeClusterMarker( root.fd(), uuid ) };
	REQUIRE( created );
	const auto marker_path { directory.path / filesystem::clusterMarkerFilename };
	for ( const auto* field : { "format", "version", "cluster_uuid", "created_at", "metadata" } )
	{
		CAPTURE( field );
		Json::Value invalid { created->document };
		invalid[ field ] = Json::Value {};
		{
			std::ofstream file { marker_path };
			file << invalid;
		}
		const auto original { markerContents( marker_path ) };
		CHECK_FALSE( filesystem::readClusterMarker( root.fd() ) );
		CHECK_FALSE( filesystem::initializeClusterMarker( root.fd(), uuid ) );
		CHECK( markerContents( marker_path ) == original );
	}
	for ( const auto* invalid : { "{", "[]", "{\"version\":1,\"version\":2}" } )
	{
		{
			std::ofstream file { marker_path };
			file << invalid;
		}
		CHECK_FALSE( filesystem::readClusterMarker( root.fd() ) );
	}
	std::filesystem::remove( marker_path );
	const auto target { directory.path / "user.json" };
	{
		std::ofstream file { target };
		file << "user file";
	}
	std::filesystem::create_symlink( target, marker_path );
	CHECK_FALSE( filesystem::initializeClusterMarker( root.fd(), uuid ) );
	CHECK( markerContents( target ) == "user file" );
}

TEST_CASE( "Racing marker creators see one complete identity", "[cluster-markers]" )
{
	MarkerDirectory directory;
	folly::File root { directory.path.c_str(), O_RDONLY | O_DIRECTORY };
	const auto uuid { boost::uuids::random_generator {}() };
	auto first { std::async(
		std::launch::async, [ &root, uuid ] { return filesystem::initializeClusterMarker( root.fd(), uuid ); } ) };
	const auto second { filesystem::initializeClusterMarker( root.fd(), boost::uuids::random_generator {}() ) };
	const auto result { first.get() };
	REQUIRE( result );
	REQUIRE( second );
	CHECK( result->uuid == second->uuid );
	CHECK(
		std::distance( std::filesystem::directory_iterator { directory.path }, std::filesystem::directory_iterator {} )
		== 1 );
}

TEST_CASE( "Verified cluster access pins the root and rejects later marker loss", "[cluster-markers]" )
{
	MarkerDirectory directory;
	const auto configured { directory.path / "root" };
	const auto moved { directory.path / "original" };
	std::filesystem::create_directory( configured );
	folly::File root { configured.c_str(), O_RDONLY | O_DIRECTORY };
	const auto uuid { boost::uuids::random_generator {}() };
	REQUIRE( filesystem::initializeClusterMarker( root.fd(), uuid ) );
	{
		std::ofstream file { configured / "sentinel" };
		file << "original";
	}
	auto access { filesystem::ClusterAccess::open( configured, uuid, false ) };
	REQUIRE( access );
	std::filesystem::rename( configured, moved );
	std::filesystem::create_directory( configured );
	CHECK( markerContents( access->root() / "sentinel" ) == "original" );
	CHECK_FALSE( filesystem::ClusterAccess::open( configured, uuid, false ) );
	CHECK( access->verify() );
	std::filesystem::remove( moved / filesystem::clusterMarkerFilename );
	CHECK_FALSE( access->verify() );
	REQUIRE( filesystem::initializeClusterMarker( root.fd(), boost::uuids::random_generator {}() ) );
	CHECK_FALSE( access->verify() );
}

TEST_CASE( "Readonly access permits absent markers but rejects conflicting identities", "[cluster-markers]" )
{
	MarkerDirectory directory;
	const auto uuid { boost::uuids::random_generator {}() };
	auto access_e { filesystem::ClusterAccess::open( directory.path, uuid, true ) };
	REQUIRE( access_e.has_value() );
	auto& access { access_e.value() };
	CHECK( access.verify().has_value() );
	CHECK( std::filesystem::is_empty( directory.path ) );
	CHECK_FALSE( filesystem::ClusterAccess::open( directory.path, uuid, false ).has_value() );

	folly::File root { directory.path.c_str(), O_RDONLY | O_DIRECTORY };
	REQUIRE( filesystem::initializeClusterMarker( root.fd(), uuid ).has_value() );
	CHECK( access.verify().has_value() );
	std::filesystem::remove( directory.path / filesystem::clusterMarkerFilename );
	CHECK( access.verify().has_value() );
	REQUIRE( filesystem::initializeClusterMarker( root.fd(), boost::uuids::random_generator {}() ).has_value() );
	CHECK_FALSE( access.verify().has_value() );
	CHECK_FALSE( filesystem::ClusterAccess::open( directory.path, uuid, true ).has_value() );
	{
		std::ofstream marker { directory.path / filesystem::clusterMarkerFilename };
		marker << "invalid JSON";
	}
	CHECK_FALSE( access.verify().has_value() );
	CHECK_FALSE( filesystem::ClusterAccess::open( directory.path / "absent", uuid, true ).has_value() );
}

TEST_CASE( "Recovery acknowledgements reset persistently while preserving config text", "[cluster-markers][config]" )
{
	MarkerDirectory directory;
	const auto path { directory.path / "config.toml" };
	const auto previous { std::string { config::getUserConfigPath() } };
	config::setLocation( path );
	const std::string before {
		"# keep this comment\n[cluster]\nrecreate_missing_markers = true # once\nname = 'unchanged'\n"
	};
	{
		std::ofstream file { path };
		file << before;
	}
	const auto consumed { config::consumeFlag( "cluster", "recreate_missing_markers" ) };
	config::setLocation( previous );
	REQUIRE( consumed );
	CHECK( *consumed );
	CHECK(
		markerContents( path )
		== "# keep this comment\n[cluster]\nrecreate_missing_markers = false # once\nname = 'unchanged'\n" );
	config::setLocation( path );
	const auto second { config::consumeFlag( "cluster", "recreate_missing_markers" ) };
	config::setLocation( previous );
	REQUIRE( second );
	CHECK_FALSE( *second );
}

} // namespace idhan::test
