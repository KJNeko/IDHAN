#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "JobEventStream.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

TEST_CASE_METHOD(
	ServerFixture,
	"Job SSE sends snapshots, final results and retention removals",
	"[api][jobs][job-events]" )
{
	const auto encoding { GENERATE( "identity", "gzip", "zstd" ) };
	CAPTURE( encoding );
	JobEventStream stream { serverPort(), api().key(), "/jobs/events", encoding };
	const auto snapshot { stream.waitFor( "snapshot" ) };
	REQUIRE( snapshot[ "jobs" ].isArray() );
	CHECK( stream.contentEncoding() == ( std::string_view { encoding } == "identity" ? "" : encoding ) );

	const auto record { api().createRecord( 8642 ) };
	Json::Value body;
	body[ "records" ] = Json::Value { Json::arrayValue };
	body[ "records" ].append( record );
	const auto dispatched { api().post( "/jobs/metadata/rescan", body ) };
	REQUIRE( dispatched.status == 200 );
	const auto job_id { dispatched.json[ "job_id" ].as< JobID >() };

	Json::Value final;
	for ( int attempt {}; attempt < 4; ++attempt )
	{
		final = stream.waitFor( "update", job_id );
		REQUIRE_FALSE( final.isNull() );
		if ( final[ "completed" ].asBool() ) break;
	}
	REQUIRE( final[ "completed" ].asBool() );
	CHECK( final[ "status" ].asString() == "completed" );
	CHECK( final[ "response" ][ "failed_count" ].asUInt64() == 1 );
	CHECK( final[ "response" ][ "failures" ][ 0 ][ "record_id" ].as< RecordID >() == record );

	JobEventStream reconnected { serverPort(), api().key(), "/jobs/events", encoding };
	const auto current { reconnected.waitFor( "snapshot" ) };
	bool retained {};
	for ( const auto& job : current[ "jobs" ] )
	{
		if ( job[ "job_id" ].as< JobID >() != job_id ) continue;

		retained = job[ "completed" ].asBool();
	}
	CHECK( retained );
	REQUIRE( api().get( std::format( "/jobs/{}/status", job_id ) ).status == 200 );
	const auto removed { stream.waitFor( "update", job_id ) };
	CHECK( removed[ "removed" ].asBool() );
}
} // namespace idhan::test
