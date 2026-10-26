#include <catch2/catch_test_macros.hpp>

#include "JobEventStream.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{
static Json::Value waitForUrlReorganization( JobEventStream& stream, const JobID id )
{
	for ( std::size_t attempt {}; attempt < 256; ++attempt )
	{
		auto event { stream.waitFor( "update", id ) };
		REQUIRE_FALSE( event.isNull() );
		if ( event[ "completed" ].asBool() ) return event;
	}

	FAIL( "URL reorganization did not finish" );
	return {};
}

TEST_CASE_METHOD( ServerFixture, "URL merges cross scan pages and commit bounded mapping batches", "[api][jobs][urls]" )
{
	{
		pqxx::work transaction { db() };
		transaction.exec( R"SQL(
			INSERT INTO records (record_id, sha256)
			SELECT n, decode(lpad(to_hex(n), 64, '0'), 'hex') FROM generate_series(1, 2506) n;
			INSERT INTO url_domains (url_domain_id, url_domain) VALUES (1, '127.0.0.1'), (2, 'unconfigured.example');
			INSERT INTO urls (url_id, url, url_domain_id, url_type) VALUES
			(1, 'http://127.0.0.1/post?b=2&a=1', 1, 'unknown'),
			(2, 'http://127.0.0.1/post?a=1&b=2', 1, 'unknown'),
			(1004, 'http://127.0.0.1/post?b=2&&a=1', 1, 'file'),
			(1006, 'http://127.0.0.1/post?b=2&a=1&', 1, 'post'),
			(1007, 'http://127.0.0.1/post?a=8&b=9', 1, 'unknown'),
			(1008, 'http://127.0.0.1/post?b=9&a=8', 1, 'post'),
			(1009, 'http://127.0.0.1/post?z=2&x=1', 1, 'file');
			INSERT INTO urls (url_id, url, url_domain_id)
			SELECT n, 'https://unconfigured.example/file/' || n || '?b=2&a=1', 2 FROM generate_series(3,1003) n;
			INSERT INTO url_mappings SELECT n, 1004 FROM generate_series(1,2505) n;
			INSERT INTO url_mappings SELECT n, 1 FROM generate_series(1,100) n;
			INSERT INTO url_mappings SELECT n, 2 FROM generate_series(1,10) n;
			INSERT INTO url_mappings VALUES (2506,1006), (1,1008);
			INSERT INTO url_log VALUES
			(1, '2020-01-01', 500), (2, '2021-01-01', 404), (1004, '2021-01-01', 200),
			(1006, '2022-01-01', 201), (1007, '2021-01-01', 204), (1008, '2021-01-01', 404);
		)SQL" );
		transaction.commit();
	}

	JobEventStream stream { serverPort(), api().key() };
	REQUIRE_FALSE( stream.waitFor( "snapshot" ).isNull() );
	const auto dispatched { api().post( "/jobs/urls/reorganize", Json::Value {} ) };
	REQUIRE( dispatched.status == 200 );
	const auto final { waitForUrlReorganization( stream, dispatched.json[ "job_id" ].as< JobID >() ) };
	INFO( final.toStyledString() );
	REQUIRE( final[ "status" ].asString() == "completed" );
	CHECK( final[ "response" ][ "total_count" ].asUInt64() == 1008 );
	CHECK( final[ "response" ][ "unique_count" ].asUInt64() == 1004 );
	CHECK( final[ "response" ][ "merged_count" ].asUInt64() == 4 );
	CHECK( final[ "response" ][ "sorted_count" ].asUInt64() == 5 );
	CHECK( final[ "progress" ][ "committed_batch_count" ].asUInt64() >= 6 );
	CHECK( final[ "progress" ][ "merged_count" ].asUInt64() == 4 );
	{
		pqxx::nontransaction transaction { db() };
		CHECK(
			transaction.query_value< std::string >( "SELECT url FROM urls WHERE url_id = 1" )
		    == "http://127.0.0.1/post?a=1&b=2" );
		CHECK( transaction.query_value< std::string >( "SELECT url_type::text FROM urls WHERE url_id = 1" ) == "file" );
		CHECK(
			transaction.query_value< std::string >( "SELECT url_type::text FROM urls WHERE url_id = 1007" ) == "post" );
		CHECK( transaction.query_value< int >( "SELECT last_code FROM url_log WHERE url_id = 1" ) == 201 );
		CHECK( transaction.query_value< int >( "SELECT last_code FROM url_log WHERE url_id = 1007" ) == 204 );
		CHECK( transaction.query_value< int >( "SELECT count(*) FROM url_mappings WHERE url_id = 1" ) == 2506 );
		CHECK(
			transaction.query_value< std::string >( "SELECT url FROM urls WHERE url_id = 3" )
		    == "https://unconfigured.example/file/3?b=2&a=1" );
	}

	const auto rerun { api().post( "/jobs/urls/reorganize", Json::Value {} ) };
	REQUIRE( rerun.status == 200 );
	const auto repeated { waitForUrlReorganization( stream, rerun.json[ "job_id" ].as< JobID >() ) };
	REQUIRE( repeated[ "status" ].asString() == "completed" );
	CHECK( repeated[ "response" ][ "merged_count" ].asUInt64() == 0 );
	CHECK( repeated[ "response" ][ "sorted_count" ].asUInt64() == 0 );
}
} // namespace idhan::test
