#include <algorithm>
#include <numeric>

#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

//! The record the search endpoint answers with for a seed's hash, or -1 when it holds no such record.
static RecordID recordFor( ApiClient& api, const int seed )
{
	const auto response { api.get( "/records/search", { { "sha256", hashFor( seed ) } } ) };

	if ( response.status != drogon::k200OK ) return -1;
	if ( !response.json[ "found" ].asBool() ) return -1;

	return response.json[ "record_id" ].asInt();
}

static int recordCount( pqxx::connection& connection )
{
	pqxx::nontransaction tx { connection };
	return tx.query_value< int >( "SELECT count(*) FROM records" );
}

SCENARIO_METHOD( ServerFixture, "Record creation", "[api][records][create]" )
{
	GIVEN( "no records" )
	{
		WHEN( "a batch of distinct hashes is posted" )
		{
			const auto record_ids { api().createRecords( { 1, 2, 3 } ) };

			THEN( "one distinct record id is returned for each hash" )
			{
				REQUIRE( record_ids.size() == 3 );
				CHECK( record_ids[ 0 ] != record_ids[ 1 ] );
				CHECK( record_ids[ 1 ] != record_ids[ 2 ] );
				CHECK( record_ids[ 0 ] != record_ids[ 2 ] );
			}

			THEN( "each id names the record holding the hash posted at that position" )
			{
				CHECK( recordFor( api(), 1 ) == record_ids[ 0 ] );
				CHECK( recordFor( api(), 2 ) == record_ids[ 1 ] );
				CHECK( recordFor( api(), 3 ) == record_ids[ 2 ] );
			}

			THEN( "the records are in the database" )
			{
				CHECK( recordCount( db() ) == 3 );
			}

			AND_WHEN( "the same hashes are posted again in a different order" )
			{
				const auto again { api().createRecords( { 3, 1, 2 } ) };

				THEN( "the ids follow the order they were posted in" )
				{
					REQUIRE( again.size() == 3 );
					CHECK( again[ 0 ] == record_ids[ 2 ] );
					CHECK( again[ 1 ] == record_ids[ 0 ] );
					CHECK( again[ 2 ] == record_ids[ 1 ] );
				}

				THEN( "no further records were created" )
				{
					CHECK( recordCount( db() ) == 3 );
				}
			}

			AND_WHEN( "one of those hashes is posted beside a new one" )
			{
				const auto mixed { api().createRecords( { 4, 2 } ) };

				THEN( "the hash that existed keeps its id and the new one gets its own" )
				{
					REQUIRE( mixed.size() == 2 );
					CHECK( mixed[ 1 ] == record_ids[ 1 ] );
					CHECK( mixed[ 0 ] != record_ids[ 0 ] );
					CHECK( mixed[ 0 ] != record_ids[ 1 ] );
					CHECK( mixed[ 0 ] != record_ids[ 2 ] );
					CHECK( mixed[ 0 ] == recordFor( api(), 4 ) );
				}

				THEN( "only the new hash added a record" )
				{
					CHECK( recordCount( db() ) == 4 );
				}
			}
		}

		WHEN( "a batch large enough to be reordered on the way to the database is posted" )
		{
			std::vector< int > seeds( 32 );
			std::iota( seeds.begin(), seeds.end(), 100 );

			const auto record_ids { api().createRecords( seeds ) };

			THEN( "every hash gets the id returned at its position" )
			{
				REQUIRE( record_ids.size() == seeds.size() );

				for ( std::size_t i = 0; i < seeds.size(); ++i )
				{
					INFO( "seed " << seeds[ i ] << " at position " << i );
					CHECK( recordFor( api(), seeds[ i ] ) == record_ids[ i ] );
				}
			}

			THEN( "no two hashes share an id" )
			{
				auto sorted { record_ids };
				std::ranges::sort( sorted );

				CHECK( std::ranges::adjacent_find( sorted ) == sorted.end() );
			}

			AND_WHEN( "the batch is posted again reversed" )
			{
				std::vector< int > reversed { seeds };
				std::ranges::reverse( reversed );

				const auto again { api().createRecords( reversed ) };

				THEN( "the ids come back reversed too" )
				{
					REQUIRE( again.size() == record_ids.size() );

					auto expected { record_ids };
					std::ranges::reverse( expected );

					CHECK( again == expected );
					CHECK( recordCount( db() ) == static_cast< int >( seeds.size() ) );
				}
			}
		}

		WHEN( "a hash appears more than once in one batch" )
		{
			const auto record_ids { api().createRecords( { 1, 2, 1 } ) };

			THEN( "every position is answered, and the repeats name the same record" )
			{
				REQUIRE( record_ids.size() == 3 );
				CHECK( record_ids[ 0 ] == record_ids[ 2 ] );
				CHECK( record_ids[ 0 ] != record_ids[ 1 ] );
				CHECK( recordCount( db() ) == 2 );
			}
		}

		WHEN( "a single hash is posted as a string" )
		{
			Json::Value body {};
			body[ "sha256" ] = hashFor( 7 );

			const auto created { api().post( "/records/create", body ) };

			THEN( "the record id comes back on its own" )
			{
				REQUIRE( created.status == drogon::k200OK );
				REQUIRE( created.json[ "record_id" ].isIntegral() );
				CHECK( created.json[ "record_id" ].asInt() == recordFor( api(), 7 ) );
				CHECK( recordCount( db() ) == 1 );
			}

			AND_WHEN( "the same hash is posted in an array" )
			{
				const auto again { api().createRecords( { 7 } ) };

				THEN( "it names the record the string form created" )
				{
					REQUIRE( again.size() == 1 );
					CHECK( again[ 0 ] == created.json[ "record_id" ].asInt() );
					CHECK( recordCount( db() ) == 1 );
				}
			}
		}

		WHEN( "a hash is not the length of a sha256" )
		{
			Json::Value hashes { Json::arrayValue };
			hashes.append( "abcdef" );

			Json::Value body {};
			body[ "sha256" ] = hashes;

			THEN( "the request is rejected" )
			{
				CHECK( api().post( "/records/create", body ).status == drogon::k400BadRequest );
			}
		}

		WHEN( "a hash is the right length but is not hex" )
		{
			Json::Value hashes { Json::arrayValue };
			hashes.append( std::string( 64, 'z' ) );

			Json::Value body {};
			body[ "sha256" ] = hashes;

			THEN( "the request is rejected" )
			{
				CHECK( api().post( "/records/create", body ).status == drogon::k400BadRequest );
			}
		}

		WHEN( "an entry of the array is not a string" )
		{
			Json::Value hashes { Json::arrayValue };
			hashes.append( hashFor( 1 ) );
			hashes.append( 7 );

			Json::Value body {};
			body[ "sha256" ] = hashes;

			const auto created { api().post( "/records/create", body ) };

			THEN( "the request is rejected, and neither entry is created" )
			{
				CHECK( created.status == drogon::k400BadRequest );
				CHECK( recordCount( db() ) == 0 );
			}
		}

		WHEN( "the body carries no sha256" )
		{
			THEN( "the request is rejected" )
			{
				CHECK( api().post( "/records/create", domainBody( "characters" ) ).status == drogon::k400BadRequest );
			}
		}

		WHEN( "the root of the body is an array" )
		{
			Json::Value body { Json::arrayValue };
			body.append( hashFor( 1 ) );

			THEN( "the request is rejected" )
			{
				CHECK( api().post( "/records/create", body ).status == drogon::k400BadRequest );
			}
		}
	}
}

} // namespace idhan::test
