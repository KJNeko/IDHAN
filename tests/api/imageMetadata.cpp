#include <format>

#include <catch2/catch_test_macros.hpp>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{
namespace
{

void seedImageMetadata( pqxx::connection& connection, const RecordID record_id, const bool with_phash )
{
	pqxx::work tx { connection };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_delete_time) VALUES ($1, 123, $2, now())",
		pqxx::params { record_id, mime_ids::IMAGE_PNG } );
	tx.exec(
		"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2)",
		pqxx::params { record_id, std::to_underlying( SimpleMimeType::IMAGE_TYPE ) } );
	if ( with_phash )
		tx.exec(
			"INSERT INTO image_metadata (record_id, width, height, channels, phash) "
			"VALUES ($1, 640, 480, 4, 'xb44dc7b24dcb381c')",
			pqxx::params { record_id } );
	else
		tx.exec(
			"INSERT INTO image_metadata (record_id, width, height, channels, phash) VALUES ($1, 640, 480, 4, NULL)",
			pqxx::params { record_id } );
	tx.commit();
}

} // namespace

SCENARIO_METHOD( ServerFixture, "Image metadata APIs expose only present perceptual hashes", "[api][metadata][phash]" )
{
	const auto hashed_id { api().createRecord( 401 ) };
	const auto blank_id { api().createRecord( 402 ) };
	seedImageMetadata( db(), hashed_id, true );
	seedImageMetadata( db(), blank_id, false );

	const auto single { api().get( std::format( "/records/{}/info", hashed_id ) ) };
	REQUIRE( single.status == drogon::k200OK );
	CHECK( single.json[ "phash" ].asString() == "b44dc7b24dcb381c" );

	Json::Value body {};
	body[ "record_ids" ] = Json::Value { Json::arrayValue };
	body[ "record_ids" ].append( hashed_id );
	body[ "record_ids" ].append( blank_id );
	const auto batch { api().post( "/records/metadata", body ) };
	REQUIRE( batch.status == drogon::k200OK );
	REQUIRE( batch.json[ "records" ].size() == 2 );

	for ( const auto& record : batch.json[ "records" ] )
	{
		if ( record[ "record_id" ].asInt() == hashed_id )
			CHECK( record[ "phash" ].asString() == "b44dc7b24dcb381c" );
		else if ( record[ "record_id" ].asInt() == blank_id )
			CHECK_FALSE( record.isMember( "phash" ) );
		else
			FAIL( "Batch returned an unexpected record" );
	}
}

SCENARIO_METHOD( ServerFixture, "Record info reports only known embedded metadata", "[api][metadata][exif]" )
{
	const auto scanned_id { api().createRecord( 403 ) };
	const auto unscanned_id { api().createRecord( 404 ) };
	seedImageMetadata( db(), scanned_id, false );
	seedImageMetadata( db(), unscanned_id, false );

	{
		pqxx::work tx { db() };
		tx.exec(
			"UPDATE image_metadata SET has_exif = TRUE, has_gps = TRUE, has_xmp = FALSE, has_iptc = FALSE, "
			"has_icc_profile = TRUE WHERE record_id = $1",
			pqxx::params { scanned_id } );
		tx.commit();
	}

	const auto scanned { api().get( std::format( "/records/{}/info", scanned_id ) ) };
	REQUIRE( scanned.status == drogon::k200OK );
	CHECK( scanned.json[ "has_exif" ].asBool() );
	CHECK( scanned.json[ "has_gps" ].asBool() );
	CHECK_FALSE( scanned.json[ "has_xmp" ].asBool() );
	CHECK_FALSE( scanned.json[ "has_iptc" ].asBool() );
	CHECK( scanned.json[ "has_icc_profile" ].asBool() );

	// An image parsed before the flags existed leaves them NULL, which is not the same as false.
	const auto unscanned { api().get( std::format( "/records/{}/info", unscanned_id ) ) };
	REQUIRE( unscanned.status == drogon::k200OK );
	CHECK_FALSE( unscanned.json.isMember( "has_exif" ) );
	CHECK_FALSE( unscanned.json.isMember( "has_icc_profile" ) );
}

} // namespace idhan::test
