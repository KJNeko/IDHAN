#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

TEST_CASE_METHOD( ServerFixture, "Storage sunburst decodes PostgreSQL table and index names", "[api][stats][sunburst]" )
{
	const auto response { api().get( "/db/stats/sunburst" ) };
	REQUIRE( response.status == 200 );
	CHECK( response.json[ "name" ].asString() == "root" );
	REQUIRE( response.json[ "children" ].isArray() );
	bool found_records { false };
	for ( const auto& table : response.json[ "children" ] )
	{
		REQUIRE( table[ "name" ].isString() );
		if ( table[ "name" ].asString() != "records" ) continue;

		found_records = true;
		CHECK( table[ "value" ].isInt64() );
		REQUIRE( table[ "children" ].isArray() );
		REQUIRE_FALSE( table[ "children" ].empty() );
		for ( const auto& index : table[ "children" ] )
		{
			CHECK( index[ "name" ].isString() );
			CHECK_FALSE( index[ "name" ].asString().empty() );
			CHECK( index[ "value" ].isInt64() );
		}
	}
	CHECK( found_records );
}

static RecordIDs reviewedSearchIds( const ApiResponse& response )
{
	REQUIRE( response.status == 200 );
	RecordIDs ids {};
	for ( const auto& id : response.json[ "record_ids" ] ) ids.push_back( id.asInt() );
	return ids;
}

TEST_CASE_METHOD(
	ServerFixture,
	"Duration sorting excludes absent durations across media and paging paths",
	"[api][review][duration]" )
{
	const auto domain { api().createDomain( "duration tests" ) };
	const auto tag { api().createTag( "duration", "included" ) };
	const auto unused_tag { api().createTag( "duration", "unused" ) };
	(void)unused_tag;
	const auto records { api().createRecords( { 151, 152, 153, 154, 155 } ) };
	const auto video { records[ 0 ] }, audio { records[ 1 ] }, animation { records[ 2 ] }, zero { records[ 3 ] };
	{
		pqxx::work tx { db() };
		for ( const auto record : records )
			tx.exec(
				"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time) VALUES ($1, 123, $2, now())",
				pqxx::params { record, mime_ids::IMAGE_PNG } );
		tx.exec(
			"INSERT INTO video_metadata (record_id, duration, framerate, width, height, bitrate, has_audio) VALUES ($1, 1.5, 24, 10, 10, 100, false)",
			pqxx::params { video } );
		tx.exec(
			"INSERT INTO audio_metadata (record_id, duration, bitrate, channels, sample_rate) VALUES ($1, 2.5, 100, 2, 44100), ($2, 0, 100, 2, 44100)",
			pqxx::params { audio, zero } );
		tx.exec(
			"INSERT INTO animation_metadata (record_id, duration, width, height, frame_count, loops) VALUES ($1, 3.5, 10, 10, 84, false)",
			pqxx::params { animation } );
		tx.commit();
	}
	REQUIRE( api().addTags( domain, records, { tag } ).status == 200 );

	Json::Value body { Json::objectValue };
	body[ "sort" ][ "by" ] = "duration";
	body[ "sort" ][ "order" ] = "asc";
	CHECK( reviewedSearchIds( api().post( "/search", body ) ) == RecordIDs { zero, video, audio, animation } );
	body[ "sort" ][ "order" ] = "desc";
	CHECK( reviewedSearchIds( api().post( "/search", body ) ) == RecordIDs { animation, audio, video, zero } );
	body[ "sort" ][ "order" ] = "asc";
	body[ "offset" ] = 1;
	body[ "limit" ] = 2;
	CHECK( reviewedSearchIds( api().post( "/search", body ) ) == RecordIDs { video, audio } );
	body[ "tag_ids" ] = Json::Value { Json::arrayValue };
	body[ "tag_ids" ].append( tag );
	CHECK( reviewedSearchIds( api().post( "/search", body ) ) == RecordIDs { video, audio } );
	body[ "tags" ] = Json::Value { Json::arrayValue };
	body[ "tags" ].append( "-duration:unused" );
	CHECK( reviewedSearchIds( api().post( "/search", body ) ) == RecordIDs { video, audio } );
}

TEST_CASE_METHOD( ServerFixture, "Tag creation and bulk mutations reject empty batches", "[api][review][validation]" )
{
	const auto domain { api().createDomain( "empty batches" ) };
	CHECK( api().post( "/tags/create", Json::Value { Json::arrayValue } ).status == 400 );
	CHECK( api().addTags( domain, {}, {} ).status == 400 );
	CHECK( api().removeTags( domain, {}, {} ).status == 400 );
	const auto record { api().createRecord( 160 ) };
	const auto tag { api().createTag( "batch", "valid" ) };
	CHECK( api().addTags( domain, { record }, { tag } ).status == 200 );
	CHECK( api().removeTags( domain, { record }, { tag } ).status == 200 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Body IDs reject zero negatives fractions and overflow before conversion",
	"[api][review][validation]" )
{
	const auto domain { api().createDomain( "ID bounds" ) };
	const auto record { api().createRecord( 161 ) };
	const auto tag { api().createTag( "id", "valid" ) };
	const std::array< Json::Value, 8 > invalid {
		Json::Value { 0 },
		Json::Value { -1 },
		Json::Value { 1.5 },
		Json::Value { "1" },
		Json::Value {},
		Json::Value { Json::Int64 { 2147483648LL } },
		Json::Value { Json::Int64 { 4294967297LL } },
		Json::Value { Json::UInt64 { 18446744073709551615ULL } }
	};
	for ( const auto& value : invalid )
	{
		CAPTURE( value.toStyledString() );
		Json::Value body { Json::objectValue };
		body[ "records" ] = Json::Value { Json::arrayValue };
		body[ "records" ].append( value );
		body[ "tags" ] = Json::Value { Json::arrayValue };
		body[ "tags" ].append( tag );
		CHECK(
			api().post( "/records/tags/add", body, { { "tag_domain_id", std::to_string( domain ) } } ).status == 400 );
		body = recordTagSetBody( { record }, { tag } );
		body[ "records" ][ 0 ] = value;
		CHECK(
			api().post( "/records/tags/remove", body, { { "tag_domain_id", std::to_string( domain ) } } ).status
			== 400 );
		body[ "records" ][ 0 ] = record;
		body[ "sets" ][ 0 ][ 0 ] = value;
		CHECK(
			api().post( "/records/tags/remove", body, { { "tag_domain_id", std::to_string( domain ) } } ).status
			== 400 );
		body = Json::Value { Json::objectValue };
		body[ "tag_ids" ] = Json::Value { Json::arrayValue };
		body[ "tag_ids" ].append( value );
		CHECK( api().post( "/search", body ).status == 400 );
		body = Json::Value { Json::objectValue };
		body[ "record_ids" ] = Json::Value { Json::arrayValue };
		body[ "record_ids" ].append( value );
		CHECK( api().post( "/records/metadata", body ).status == 400 );
		Json::Value pairs { Json::arrayValue };
		Json::Value pair { Json::objectValue };
		pair[ "parent_id" ] = tag;
		pair[ "child_id" ] = value;
		pairs.append( pair );
		CHECK(
			api().post( "/tags/parents/create", pairs, { { "tag_domain_id", std::to_string( domain ) } } ).status
			== 400 );
	}
	for ( const auto invalid_domain : { 0, -1, 32768, 65537 } )
	{
		Json::Value body { Json::objectValue };
		body[ "tag_domains" ] = Json::Value { Json::arrayValue };
		body[ "tag_domains" ].append( invalid_domain );
		CHECK( api().post( "/search", body ).status == 400 );
	}
	CHECK( api().get( "/records/0/info" ).status == 400 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"JSON requests larger than the spill threshold remain usable",
	"[api][review][large-body]" )
{
	const std::string padding( 9 * 1024 * 1024, 'x' );
	Json::Value body { Json::objectValue };
	body[ "padding" ] = padding;
	CHECK( api().post( "/search", body ).status == 200 );
	body[ "record_ids" ] = Json::Value { Json::arrayValue };
	CHECK( api().post( "/records/metadata", body ).status == 200 );
	body[ "id" ] = "1f7893ba-1305-42af-bc99-4862ab219b29";
	body[ "name" ] = "Large review layout";
	body[ "schema" ] = 1;
	REQUIRE( api().post( "/layouts", body ).status == 201 );
	const auto layouts { api().get( "/layouts" ) };
	REQUIRE( layouts.status == 200 );
	REQUIRE( layouts.json.size() == 1 );
	CHECK( layouts.json[ 0 ][ "created_at" ].isIntegral() );
	CHECK( layouts.json[ 0 ][ "updated_at" ].isIntegral() );
	CHECK( api().del( "/layouts/1f7893ba-1305-42af-bc99-4862ab219b29" ).status == 200 );
	CHECK( api().postOctets( "/search", "{" + padding ).status == 400 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Search documents tag precedence negative forms and debug stats",
	"[api][review][search]" )
{
	const auto domain { api().createDomain( "search contract" ) };
	const auto tag { api().createTag( "review", "known" ) };
	const auto records { api().createRecords( { 162, 163 } ) };
	{
		pqxx::work tx { db() };
		for ( const auto record : records )
			tx.exec(
				"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time) VALUES ($1, 123, $2, now())",
				pqxx::params { record, mime_ids::IMAGE_PNG } );
		tx.commit();
	}
	REQUIRE( api().addTags( domain, { records[ 0 ] }, { tag } ).status == 200 );
	Json::Value body { Json::objectValue };
	body[ "tags" ] = Json::Value { Json::arrayValue };
	body[ "tags" ].append( "-review:known" );
	body[ "tag_ids" ] = Json::Value { Json::arrayValue };
	body[ "tag_ids" ].append( 0 );
	body[ "debug" ] = true;
	const auto response { api().post( "/search", body ) };
	CHECK( reviewedSearchIds( response ) == RecordIDs { records[ 1 ] } );
	REQUIRE( response.json[ "stats" ].isArray() );
	for ( const auto& step : response.json[ "stats" ] )
	{
		CHECK( step[ "step" ].isString() );
		CHECK( step[ "rows" ].isIntegral() );
		CHECK( step[ "inverted" ].isBool() );
		CHECK( step[ "kind" ].isString() );
	}
	body[ "tags" ][ 0 ] = "-review:k*";
	CHECK( reviewedSearchIds( api().post( "/search", body ) ) == RecordIDs { records[ 1 ] } );
	body[ "tags" ][ 0 ] = "-review:*";
	CHECK( api().post( "/search", body ).status == 400 );
	body[ "tags" ][ 0 ] = "missing:tag";
	CHECK( api().post( "/search", body ).status == 404 );
}

} // namespace idhan::test
