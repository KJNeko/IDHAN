#include <catch2/catch_test_macros.hpp>

#include <format>
#include <optional>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

//! A searchable image record whose embedded metadata flags are whatever the caller passes; a nullopt
//! flag is an image parsed before those blocks were looked for.
static void seedFlaggedImage(
	pqxx::connection& connection,
	const RecordID record_id,
	const std::optional< bool > exif,
	const std::optional< bool > xmp,
	const std::optional< bool > icc_profile )
{
	pqxx::work tx { connection };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, cluster_delete_time) "
		"VALUES ($1, 123, $2, now(), now())",
		pqxx::params { record_id, mime_ids::IMAGE_PNG } );
	tx.exec(
		"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2)",
		pqxx::params { record_id, std::to_underlying( SimpleMimeType::IMAGE_TYPE ) } );
	tx.exec(
		"INSERT INTO image_metadata (record_id, width, height, channels, has_exif, has_xmp, has_icc_profile) "
		"VALUES ($1, 640, 480, 4, $2, $3, $4)",
		pqxx::params { record_id, exif, xmp, icc_profile } );
	tx.commit();
}

static RecordIDs search( ApiClient& api, const std::string& predicate )
{
	Json::Value tags { Json::arrayValue };
	tags.append( predicate );

	Json::Value body {};
	body[ "tags" ] = std::move( tags );

	const auto response { api.post( "/search", body ) };

	if ( response.status != drogon::k200OK )
		throw std::runtime_error( std::format( "Search failed: {}", response.body ) );

	RecordIDs ids {};
	for ( const auto& id : response.json[ "record_ids" ] ) ids.emplace_back( id.asInt() );

	std::sort( ids.begin(), ids.end() );

	return ids;
}

static RecordIDs sortedIds( RecordIDs ids )
{
	std::sort( ids.begin(), ids.end() );
	return ids;
}

SCENARIO_METHOD( ServerFixture, "system: predicates search embedded metadata", "[api][search][exif]" )
{
	GIVEN( "images carrying different embedded metadata blocks" )
	{
		const auto records { api().createRecords( { 31, 32, 33, 34 } ) };
		const auto exif_image { records[ 0 ] };
		const auto xmp_image { records[ 1 ] };
		const auto bare_image { records[ 2 ] };
		const auto unscanned_image { records[ 3 ] };

		seedFlaggedImage( db(), exif_image, true, false, true );
		seedFlaggedImage( db(), xmp_image, false, true, false );
		seedFlaggedImage( db(), bare_image, false, false, false );
		seedFlaggedImage( db(), unscanned_image, std::nullopt, std::nullopt, std::nullopt );

		WHEN( "exif is asked for" )
		{
			THEN( "only the image carrying it is returned" )
			{
				CHECK( search( api(), "system:has exif" ) == RecordIDs { exif_image } );
			}
		}

		WHEN( "the absence of exif is asked for" )
		{
			THEN( "an image never parsed for it is left out along with the one that has it" )
			{
				CHECK( search( api(), "system:no exif" ) == sortedIds( { xmp_image, bare_image } ) );
			}
		}

		WHEN( "any embedded metadata is asked for" )
		{
			THEN( "xmp counts as well as exif" )
			{
				CHECK( search( api(), "system:has embedded metadata" ) == sortedIds( { exif_image, xmp_image } ) );
			}
		}

		WHEN( "an icc profile is asked for" )
		{
			THEN( "only the image carrying one is returned" )
			{
				CHECK( search( api(), "system:has icc profile" ) == RecordIDs { exif_image } );
			}
		}
	}
}

} // namespace idhan::test
