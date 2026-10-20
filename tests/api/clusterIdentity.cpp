#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <barrier>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>

#include "ServerFixture.hpp"

namespace idhan::test
{

class ClusterIdentityFixture : public ServerFixture
{
	std::vector< ClusterID > m_clusters {};

  public:

	const std::string prefix;
	const std::filesystem::path directory;

	ClusterIdentityFixture() :
	  prefix( std::format( "idhan-cluster-identity-{}-{}", ::getpid(), nextDirectory() ) ),
	  directory( std::filesystem::temp_directory_path() / prefix )
	{
		if ( !std::filesystem::create_directory( directory ) )
			throw std::runtime_error( "Cluster test directory must be new" );
	}

	~ClusterIdentityFixture()
	{
		for ( const auto cluster_id : m_clusters )
		{
			try
			{
				const auto response { api().del( std::format( "/clusters/{}/remove", cluster_id ) ) };
				CHECK( response.status == 200 );
			}
			catch ( const std::exception& error )
			{
				WARN( error.what() );
			}
		}
		std::error_code error;
		std::filesystem::remove_all( directory, error );
	}

	static unsigned nextDirectory()
	{
		static unsigned next {};
		return ++next;
	}

	Json::Value body( const std::string& name, const std::filesystem::path& path, const bool readonly = true ) const
	{
		Json::Value value { Json::objectValue };
		value[ "name" ] = prefix + "-" + name;
		value[ "path" ] = path.string();
		value[ "readonly" ] = readonly;
		return value;
	}

	void remember( const ApiResponse& response )
	{
		if ( response.status == 201 )
			m_clusters.push_back( static_cast< ClusterID >( response.json[ "cluster_id" ].asInt() ) );
	}

	ClusterID create( const std::string& name, const std::filesystem::path& path )
	{
		std::filesystem::create_directories( path );
		const auto response { api().post( "/clusters/add", body( name, path ) ) };
		remember( response );
		REQUIRE( response.status == 201 );
		return static_cast< ClusterID >( response.json[ "cluster_id" ].asInt() );
	}

	void remember( const ClusterID id ) { m_clusters.push_back( id ); }
};

TEST_CASE_METHOD( ClusterIdentityFixture, "Cluster names are checked before filesystem work", "[api][cluster-identity]" )
{
	create( "archive", directory / "original" );
	const auto absent { directory / "must-not-be-created" };
	const auto duplicate { api().post( "/clusters/add", body( "archive", absent, false ) ) };
	CHECK( duplicate.status == 409 );
	CHECK( duplicate.body.find( "name" ) != std::string::npos );
	CHECK_FALSE( std::filesystem::exists( absent ) );

	const auto existing { directory / "existing" };
	std::filesystem::create_directory( existing );
	const auto sentinel { existing / "write_test.txt" };
	{
		std::ofstream file { sentinel };
		file << "user file";
	}
	const auto conflict { api().post( "/clusters/add", body( "archive", existing, false ) ) };
	CHECK( conflict.status == 409 );
	std::ifstream file { sentinel };
	std::string contents;
	std::getline( file, contents );
	CHECK( contents == "user file" );

	const auto distinct { api().post( "/clusters/add", body( "distinct", existing ) ) };
	remember( distinct );
	CHECK( distinct.status == 201 );
}

TEST_CASE_METHOD( ClusterIdentityFixture, "Cluster paths use absolute lexical normalization", "[api][cluster-identity]" )
{
	const auto path { directory / "archive" };
	std::filesystem::create_directory( path );
	const auto relative { path.lexically_relative( std::filesystem::current_path() ) };
	const auto spelling { relative / "unused" / ".." / "." / "" };
	const auto created { api().post( "/clusters/add", body( "archive", spelling ) ) };
	remember( created );
	REQUIRE( created.status == 201 );
	CHECK( created.json[ "path" ].asString() == path.string() );
	for ( const auto& alias : { path, relative, path / ".", path / "child" / "..", path / "" } )
	{
		CAPTURE( alias.string() );
		const auto conflict { api().post( "/clusters/add", body( "different", alias ) ) };
		CHECK( conflict.status == 409 );
		CHECK( conflict.body.find( "Path" ) != std::string::npos );
	}
}

TEST_CASE_METHOD( ClusterIdentityFixture, "Legacy relative cluster paths participate in uniqueness", "[api][cluster-identity]" )
{
	const auto path { directory / "legacy" };
	std::filesystem::create_directory( path );
	const auto spelling { path.lexically_relative( std::filesystem::current_path() ) / "." / "" };
	pqxx::work insert { db() };
	const auto id { insert.query_value< ClusterID >(
		"INSERT INTO file_clusters (cluster_name, folder_path) VALUES ($1, $2) RETURNING cluster_id",
		pqxx::params { prefix + "-legacy", spelling.string() } ) };
	insert.commit();
	remember( id );
	const auto conflict { api().post( "/clusters/add", body( "new", path ) ) };
	CHECK( conflict.status == 409 );
}

TEST_CASE_METHOD( ClusterIdentityFixture, "Cluster renames preserve name uniqueness", "[api][cluster-identity]" )
{
	const auto first { create( "first", directory / "first" ) };
	const auto second { create( "second", directory / "second" ) };
	Json::Value change { Json::objectValue };
	change[ "name" ] = prefix + "-first";
	CHECK( api().patch( std::format( "/clusters/{}/modify", second ), change ).status == 409 );
	CHECK( api().patch( std::format( "/clusters/{}/modify", first ), change ).status == 200 );
	change[ "name" ] = prefix + "-renamed";
	CHECK( api().patch( std::format( "/clusters/{}/modify", second ), change ).status == 200 );
}

TEST_CASE_METHOD( ClusterIdentityFixture, "Concurrent cluster creations retain database uniqueness", "[api][cluster-identity]" )
{
	const auto first_path { directory / "first" };
	const auto second_path { directory / "second" };
	std::filesystem::create_directory( first_path );
	std::filesystem::create_directory( second_path );
	std::barrier start { 2 };
	auto first { std::async( std::launch::async, [ this, &start, first_path ] {
		start.arrive_and_wait();
		return api().post( "/clusters/add", body( "shared", first_path ) );
	} ) };
	start.arrive_and_wait();
	const auto second { api().post( "/clusters/add", body( "shared", second_path ) ) };
	const auto first_result { first.get() };
	remember( first_result );
	remember( second );
	CHECK( ( first_result.status == 201 || second.status == 201 ) );
	CHECK( ( first_result.status == 409 || second.status == 409 ) );
}

TEST_CASE_METHOD( ClusterIdentityFixture, "Concurrent cluster renames report a name conflict", "[api][cluster-identity]" )
{
	const auto first_id { create( "first", directory / "first" ) };
	const auto second_id { create( "second", directory / "second" ) };
	Json::Value change { Json::objectValue };
	change[ "name" ] = prefix + "-shared";
	std::barrier start { 2 };
	auto first { std::async( std::launch::async, [ this, &start, first_id, &change ] {
		start.arrive_and_wait();
		return api().patch( std::format( "/clusters/{}/modify", first_id ), change );
	} ) };
	start.arrive_and_wait();
	const auto second { api().patch( std::format( "/clusters/{}/modify", second_id ), change ) };
	const auto first_result { first.get() };
	CHECK( ( first_result.status == 200 || second.status == 200 ) );
	CHECK( ( first_result.status == 409 || second.status == 409 ) );
}

TEST_CASE_METHOD( ClusterIdentityFixture, "Concurrent normalized cluster paths conflict", "[api][cluster-identity]" )
{
	const auto path { directory / "shared" };
	std::filesystem::create_directory( path );
	std::barrier start { 2 };
	auto first { std::async( std::launch::async, [ this, &start, path ] {
		start.arrive_and_wait();
		return api().post( "/clusters/add", body( "first", path / "." ) );
	} ) };
	start.arrive_and_wait();
	const auto second { api().post( "/clusters/add", body( "second", path / "child" / ".." / "" ) ) };
	const auto first_result { first.get() };
	remember( first_result );
	remember( second );
	CHECK( ( first_result.status == 201 || second.status == 201 ) );
	CHECK( ( first_result.status == 409 || second.status == 409 ) );
}

} // namespace idhan::test
