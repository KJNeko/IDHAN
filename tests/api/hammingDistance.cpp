#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <map>
#include <thread>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

using DistancePair = std::pair< RecordID, RecordID >;
using Distances = std::map< DistancePair, int >;

static void seedHashedImage( pqxx::connection& connection, const RecordID record_id, const std::string& hex )
{
	pqxx::work tx { connection };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, cluster_delete_time) "
		"VALUES ($1, 123, $2, now(), now())",
		pqxx::params { record_id, mime_ids::IMAGE_PNG } );
	tx.exec(
		"INSERT INTO image_metadata (record_id, width, height, channels, phash) "
		"VALUES ($1, 640, 480, 4, $2::bit(64))",
		pqxx::params { record_id, "x" + hex } );
	tx.commit();
}

static Distances storedDistances( pqxx::connection& connection )
{
	pqxx::nontransaction tx { connection };

	Distances distances {};
	for ( const auto& [ left_id, right_id, distance ] :
	      tx.query< RecordID, RecordID, int >( "SELECT left_id, right_id, distance FROM hamming_distance" ) )
		distances.emplace( DistancePair { left_id, right_id }, distance );

	return distances;
}

//! The sweep runs on a timer, so the assertions wait for it rather than for a request to finish.
static Distances awaitDistances( pqxx::connection& connection, const std::size_t expected )
{
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds( 40 ) };

	Distances distances { storedDistances( connection ) };
	while ( distances.size() < expected && std::chrono::steady_clock::now() < deadline )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
		distances = storedDistances( connection );
	}

	return distances;
}

static bool queueDrained( pqxx::connection& connection )
{
	pqxx::nontransaction tx { connection };
	return tx.query_value< bool >( "SELECT NOT EXISTS (SELECT 1 FROM hamming_distance_queue)" );
}

SCENARIO_METHOD( ServerFixture, "The server stores hamming distances for newly hashed records", "[api][phash]" )
{
	GIVEN( "records whose perceptual hashes sit at known distances apart" )
	{
		const auto records { api().createRecords( { 41, 42, 43, 44 } ) };

		const auto base { records[ 0 ] };
		const auto one_bit { records[ 1 ] };
		const auto eight_bits { records[ 2 ] };
		const auto distant { records[ 3 ] };

		seedHashedImage( db(), base, "0000000000000000" );
		seedHashedImage( db(), one_bit, "0000000000000001" );
		seedHashedImage( db(), eight_bits, "00000000000000ff" );
		seedHashedImage( db(), distant, "ffffffffffffffff" );

		WHEN( "the sweep has run" )
		{
			const auto distances { awaitDistances( db(), 3 ) };

			THEN( "every pair within eight bits is stored once, in id order" )
			{
				REQUIRE( distances.size() == 3 );
				CHECK( distances.at( { std::min( base, one_bit ), std::max( base, one_bit ) } ) == 1 );
				CHECK( distances.at( { std::min( base, eight_bits ), std::max( base, eight_bits ) } ) == 8 );
				CHECK( distances.at( { std::min( one_bit, eight_bits ), std::max( one_bit, eight_bits ) } ) == 7 );
			}

			THEN( "the pair beyond eight bits is discarded" )
			{
				for ( const auto& [ pair, distance ] : distances )
				{
					CHECK( pair.first != distant );
					CHECK( pair.second != distant );
				}
			}

			THEN( "the queue is drained" )
			{
				CHECK( queueDrained( db() ) );
			}
		}
	}
}

} // namespace idhan::test
