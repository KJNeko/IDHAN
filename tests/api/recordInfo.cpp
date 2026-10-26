#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <utility>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

static ApiResponse fetchRecordInfoBatch( ApiClient& api, const RecordIDs& record_ids )
{
	Json::Value body {};
	body[ "record_ids" ] = Json::Value { Json::arrayValue };
	for ( const auto record_id : record_ids ) body[ "record_ids" ].append( record_id );
	return api.post( "/records/metadata", body );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Record info returns known file facts independently of MIME",
	"[api][metadata][file-info]" )
{
	const auto records { api().createRecords( { 1901, 1902, 1903, 1904, 1905 } ) };
	{
		pqxx::work tx { db() };
		tx.exec(
			"INSERT INTO file_info (record_id, size, extension, file_mtime, file_ctime) "
			"VALUES ($1, 4294967296, 'bin', TIMESTAMP '1970-01-01 00:00:01.234567', TIMESTAMP '1970-01-01 00:00:02.345678'), "
			"($2, 0, 'bin', TIMESTAMP '1970-01-01 00:00:00', NULL), "
			"($3, 17, 'bin', NULL, TIMESTAMP '1970-01-01 00:00:00')",
			pqxx::params { records[ 1 ], records[ 2 ], records[ 3 ] } );
		tx.exec(
			"INSERT INTO file_info (record_id, size, mime_id) VALUES ($1, 321, $2)",
			pqxx::params { records[ 4 ], mime_ids::IMAGE_PNG } );
		tx.commit();
	}

	const std::array< std::optional< std::int64_t >, 5 > sizes { std::nullopt, 4294967296LL, 0, 17, 321 };
	const std::array< std::optional< std::int64_t >, 5 > mtimes {
		std::nullopt, 1234567, 0, std::nullopt, std::nullopt
	};
	const std::array< std::optional< std::int64_t >, 5 > ctimes {
		std::nullopt, 2345678, std::nullopt, 0, std::nullopt
	};
	const auto batch { fetchRecordInfoBatch( api(), records ) };
	REQUIRE( batch.status == 200 );
	REQUIRE( batch.json[ "records" ].size() == records.size() );
	CHECK( batch.json[ "missing" ].empty() );

	for ( Json::ArrayIndex index = 0; index < records.size(); ++index )
	{
		CAPTURE( records[ index ] );
		const auto single { api().get( std::format( "/records/{}/info", records[ index ] ) ) };
		REQUIRE( single.status == 200 );
		CHECK( batch.json[ "records" ][ index ] == single.json );
		const auto& record { single.json };
		CHECK( record[ "record_id" ].asInt() == records[ index ] );
		CHECK( record.isMember( "size" ) == sizes[ index ].has_value() );
		CHECK( record.isMember( "file_mtime" ) == mtimes[ index ].has_value() );
		CHECK( record.isMember( "file_ctime" ) == ctimes[ index ].has_value() );
		if ( sizes[ index ] ) CHECK( record[ "size" ].asInt64() == *sizes[ index ] );
		if ( mtimes[ index ] ) CHECK( record[ "file_mtime" ].asInt64() == *mtimes[ index ] );
		if ( ctimes[ index ] ) CHECK( record[ "file_ctime" ].asInt64() == *ctimes[ index ] );

		CHECK_FALSE( record.isMember( "simple_type" ) );
		CHECK_FALSE( record.isMember( "width" ) );
		CHECK_FALSE( record.isMember( "duration" ) );
		if ( index == 4 )
		{
			CHECK( record[ "mime" ].asString() == "image/png" );
			CHECK( record[ "extension" ].asString() == "png" );
			REQUIRE( record[ "parsed" ].isBool() );
			CHECK_FALSE( record[ "parsed" ].asBool() );
		}
		else
		{
			CHECK_FALSE( record.isMember( "mime" ) );
			CHECK_FALSE( record.isMember( "extension" ) );
			CHECK_FALSE( record.isMember( "parsed" ) );
		}
	}
}

TEST_CASE_METHOD(
	ServerFixture,
	"Record info preserves parsed audio and animation fields",
	"[api][metadata][file-info]" )
{
	const auto records { api().createRecords( { 1911, 1912 } ) };
	{
		pqxx::work tx { db() };
		const auto audio_mime { tx.query_value< MimeID >(
			"INSERT INTO mime (name, best_extension) VALUES ('audio/x-idhan-f19-test', 'audio') RETURNING mime_id" ) };
		tx.exec(
			"INSERT INTO file_info (record_id, size, mime_id, file_mtime) "
			"VALUES ($1, 123, $2, TIMESTAMP '1970-01-01 00:00:00'), "
			"($3, 123, $4, TIMESTAMP '1970-01-01 00:00:00')",
			pqxx::params { records[ 0 ], audio_mime, records[ 1 ], mime_ids::ANIMATION_GIF } );
		tx.exec(
			"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2), ($3, $4)",
			pqxx::params {
				records[ 0 ],
				std::to_underlying( SimpleMimeType::AUDIO ),
				records[ 1 ],
				std::to_underlying( SimpleMimeType::ANIMATION ) } );
		tx.exec(
			"INSERT INTO audio_metadata (record_id, duration, bitrate, channels, sample_rate) "
			"VALUES ($1, 2.5, 192000, 2, 48000)",
			pqxx::params { records[ 0 ] } );
		tx.exec(
			"INSERT INTO animation_metadata (record_id, width, height, frame_count, duration, loops) "
			"VALUES ($1, 640, 480, 60, 2.5, false)",
			pqxx::params { records[ 1 ] } );
		tx.commit();
	}

	const auto batch { fetchRecordInfoBatch( api(), records ) };
	REQUIRE( batch.status == 200 );
	REQUIRE( batch.json[ "records" ].size() == 2 );
	for ( Json::ArrayIndex index = 0; index < records.size(); ++index )
	{
		const auto single { api().get( std::format( "/records/{}/info", records[ index ] ) ) };
		REQUIRE( single.status == 200 );
		CHECK( single.json == batch.json[ "records" ][ index ] );
		CHECK( single.json[ "parsed" ].asBool() );
		CHECK( single.json[ "size" ].asInt64() == 123 );
		CHECK( single.json[ "file_mtime" ].asInt64() == 0 );
		CHECK( single.json[ "duration" ].asDouble() == 2.5 );
	}
	const auto& audio { batch.json[ "records" ][ 0 ] };
	CHECK( audio[ "simple_type" ].asString() == "audio" );
	CHECK( audio[ "channels" ].asInt() == 2 );
	CHECK( audio[ "bitrate" ].asInt() == 192000 );
	CHECK( audio[ "sample_rate" ].asInt() == 48000 );
	const auto& animation { batch.json[ "records" ][ 1 ] };
	CHECK( animation[ "simple_type" ].asString() == "animation" );
	CHECK( animation[ "width" ].asInt() == 640 );
	CHECK( animation[ "height" ].asInt() == 480 );
	CHECK( animation[ "frame_count" ].asInt() == 60 );
	CHECK_FALSE( animation[ "loops" ].asBool() );
}

} // namespace idhan::test
