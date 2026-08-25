#include <catch2/catch_test_macros.hpp>

#include <format>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

//! A searchable image record: file_info gives it a mime, image_metadata the hash to compare.
static void seedHashedImage( pqxx::connection& connection, const RecordID record_id, const std::string& hex )
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
		"INSERT INTO image_metadata (record_id, width, height, channels, phash) "
		"VALUES ($1, 640, 480, 4, $2::bit(64))",
		pqxx::params { record_id, hex.empty() ? std::optional< std::string > {} : std::optional { "x" + hex } } );
	tx.commit();
}

//! Record ids arrive from the create endpoint in hash order, so an expectation has to be sorted too.
static RecordIDs sortedIds( RecordIDs ids )
{
	std::sort( ids.begin(), ids.end() );
	return ids;
}

static RecordIDs nearby( ApiClient& api, const std::string& predicate )
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

SCENARIO_METHOD( ServerFixture, "system:nearby searches by perceptual hash distance", "[api][search][phash]" )
{
	GIVEN( "records whose perceptual hashes sit at known distances apart" )
	{
		const auto records { api().createRecords( { 21, 22, 23, 24, 25 } ) };
		const auto probe { records[ 0 ] };
		const auto one_bit { records[ 1 ] };
		const auto three_bits { records[ 2 ] };
		const auto distant { records[ 3 ] };
		const auto unhashed { records[ 4 ] };

		seedHashedImage( db(), probe, "b44dc7b24dcb381c" );
		seedHashedImage( db(), one_bit, "b44dc7b24dcb381d" );
		seedHashedImage( db(), three_bits, "b44dc7b24dcb381b" );
		seedHashedImage( db(), distant, "deadbeefdeadbeef" );
		seedHashedImage( db(), unhashed, {} );

		WHEN( "a distance wide enough for both near neighbours is given" )
		{
			const auto found { nearby( api(), std::format( "system:nearby {} distance 3", probe ) ) };

			THEN( "they are returned along with the probe itself" )
			{
				CHECK( found == sortedIds( { probe, one_bit, three_bits } ) );
			}
		}

		WHEN( "a tighter distance is given" )
		{
			THEN( "only the neighbours within it are returned" )
			{
				CHECK(
					nearby( api(), std::format( "system:nearby {} distance 1", probe ) )
					== sortedIds( { probe, one_bit } ) );
				CHECK( nearby( api(), std::format( "system:nearby {} distance 0", probe ) ) == RecordIDs { probe } );
			}
		}

		WHEN( "the distance clause is left off" )
		{
			THEN( "the predicate matches an exact hash only" )
			{
				CHECK( nearby( api(), std::format( "system:nearby {}", probe ) ) == RecordIDs { probe } );
			}
		}

		WHEN( "the search is run from a neighbour" )
		{
			const auto found { nearby( api(), std::format( "system:nearby {} distance 1", one_bit ) ) };

			THEN( "distance is measured from that record instead" )
			{
				CHECK( found == sortedIds( { probe, one_bit } ) );
			}
		}

		WHEN( "the whole hash width is allowed" )
		{
			const auto found { nearby( api(), std::format( "system:nearby {} distance 64", probe ) ) };

			THEN( "every hashed record matches, and only those" )
			{
				CHECK( found == sortedIds( { probe, one_bit, three_bits, distant } ) );
			}
		}

		WHEN( "the probe carries no perceptual hash" )
		{
			THEN( "nothing is within any distance of it" )
			{
				CHECK( nearby( api(), std::format( "system:nearby {} distance 64", unhashed ) ).empty() );
			}
		}

		WHEN( "a record that does not exist is named" )
		{
			THEN( "the search is empty rather than an error" )
			{
				CHECK( nearby( api(), "system:nearby 999999 distance 8" ).empty() );
			}
		}
	}
}

SCENARIO_METHOD( ServerFixture, "system:nearby rejects malformed predicates", "[api][search][phash]" )
{
	const auto rejected {
		[ this ]( const std::string& predicate )
		{
			Json::Value tags { Json::arrayValue };
			tags.append( predicate );
			Json::Value body {};
			body[ "tags" ] = std::move( tags );
			return api().post( "/search", body ).status;
		}
	};

	CHECK( rejected( "system:nearby" ) == drogon::k400BadRequest );
	CHECK( rejected( "system:nearby abc" ) == drogon::k400BadRequest );
	CHECK( rejected( "system:nearby 0" ) == drogon::k400BadRequest );
	CHECK( rejected( "system:nearby 12 distance" ) == drogon::k400BadRequest );
	CHECK( rejected( "system:nearby 12 distance 65" ) == drogon::k400BadRequest );
	CHECK( rejected( "system:nearby 12 within 4" ) == drogon::k400BadRequest );
	CHECK( rejected( "system:nearby 12 distance 4 more" ) == drogon::k400BadRequest );
}

} // namespace idhan::test
