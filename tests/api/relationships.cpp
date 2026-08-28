#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "harness/ServerFixture.hpp"

namespace idhan::test
{

static Json::Value duplicateBody( const RecordID worse, const RecordID better )
{
	Json::Value json {};
	json[ "worse_id" ] = worse;
	json[ "better_id" ] = better;
	return json;
}

SCENARIO_METHOD( ServerFixture, "Flat duplicate groups can be read", "[api][relationships]" )
{
	GIVEN( "a duplicate king with three lesser records and an alternative" )
	{
		const auto records { api().createRecords( { 1, 2, 3, 4, 5 } ) };
		const auto superior { records[ 0 ] };
		const auto first_inferior { records[ 1 ] };
		const auto second_inferior { records[ 2 ] };
		const auto alternative { records[ 3 ] };
		const auto deep_inferior { records[ 4 ] };

		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( first_inferior, superior ) ).status
			== drogon::k200OK );
		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( second_inferior, superior ) ).status
			== drogon::k200OK );
		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( deep_inferior, superior ) ).status
			== drogon::k200OK );

		Json::Value alternatives { Json::arrayValue };
		alternatives.append( superior );
		alternatives.append( alternative );
		REQUIRE( api().post( "/relationships/alternatives/add", alternatives ).status == drogon::k200OK );

		WHEN( "the superior file's relationships are requested" )
		{
			const auto response { api().get( "/relationships/" + std::to_string( superior ) ) };

			THEN( "all its inferior copies and its alternative are returned by role" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK( response.json[ "inferior" ].size() == 3 );
				CHECK( response.json[ "superior" ].empty() );
				REQUIRE( response.json[ "alternatives" ].size() == 1 );
				CHECK( response.json[ "alternatives" ][ 0 ].asInt() == alternative );

				const auto alternative_response { api().get( "/relationships/" + std::to_string( alternative ) ) };
				REQUIRE( alternative_response.status == drogon::k200OK );
				REQUIRE( alternative_response.json[ "alternatives" ].size() == 1 );
				CHECK( alternative_response.json[ "alternatives" ][ 0 ].asInt() == superior );
			}
		}

		WHEN( "an inferior file's relationships are requested" )
		{
			const auto response { api().get( "/relationships/" + std::to_string( first_inferior ) ) };

			THEN( "its superior is returned" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK( response.json[ "inferior" ].empty() );
				REQUIRE( response.json[ "superior" ].size() == 1 );
				CHECK( response.json[ "superior" ][ 0 ].asInt() == superior );
				CHECK( response.json[ "alternatives" ].empty() );
			}
		}

		WHEN( "another lesser file's relationships are requested" )
		{
			const auto response { api().get( "/relationships/" + std::to_string( deep_inferior ) ) };

			THEN( "only its king is returned" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "superior" ].size() == 1 );
				CHECK( response.json[ "superior" ][ 0 ].asInt() == superior );
				CHECK( response.json[ "inferior" ].empty() );
			}
		}
	}
}

SCENARIO_METHOD( ServerFixture, "Duplicate groups keep one explicit king", "[api][relationships]" )
{
	GIVEN( "two flat duplicate groups" )
	{
		const auto records { api().createRecords( { 61, 62, 63, 64 } ) };
		const auto first_king { records[ 0 ] };
		const auto first_lesser { records[ 1 ] };
		const auto second_king { records[ 2 ] };
		const auto second_lesser { records[ 3 ] };

		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( first_lesser, first_king ) ).status
			== drogon::k200OK );
		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( second_lesser, second_king ) ).status
			== drogon::k200OK );

		WHEN( "a lesser member is promoted" )
		{
			Integer original_duplicate_id {};
			{
				pqxx::nontransaction tx { db() };
				original_duplicate_id = tx.query_value< Integer >(
					"SELECT duplicate_id FROM duplicate_groups WHERE king_id = $1", pqxx::params { first_king } );
			}

			const auto response {
				api().post( "/relationships/duplicates/add", duplicateBody( first_king, first_lesser ) )
			};

			THEN( "it becomes the only king and every other member points directly to it" )
			{
				REQUIRE( response.status == drogon::k200OK );

				const auto king { api().get( "/relationships/" + std::to_string( first_lesser ) ) };
				REQUIRE( king.status == drogon::k200OK );
				REQUIRE( king.json[ "inferior" ].size() == 1 );
				CHECK( king.json[ "inferior" ][ 0 ].asInt() == first_king );
				CHECK( king.json[ "superior" ].empty() );

				pqxx::nontransaction tx { db() };
				CHECK(
					tx.query_value< Integer >(
						"SELECT duplicate_id FROM duplicate_groups WHERE king_id = $1", pqxx::params { first_lesser } )
					== original_duplicate_id );
			}
		}

		WHEN( "the groups are joined under a member of the second group" )
		{
			Integer second_duplicate_id {};
			{
				pqxx::nontransaction tx { db() };
				second_duplicate_id = tx.query_value< Integer >(
					"SELECT duplicate_id FROM duplicate_groups WHERE king_id = $1", pqxx::params { second_king } );
			}

			const auto response {
				api().post( "/relationships/duplicates/add", duplicateBody( first_king, second_lesser ) )
			};

			THEN( "that member is king and the merged group has no chain or cycle" )
			{
				REQUIRE( response.status == drogon::k200OK );

				const auto king { api().get( "/relationships/" + std::to_string( second_lesser ) ) };
				REQUIRE( king.status == drogon::k200OK );
				CHECK( king.json[ "inferior" ].size() == 3 );
				CHECK( king.json[ "superior" ].empty() );

				for ( const auto id : { first_king, first_lesser, second_king } )
				{
					const auto lesser { api().get( "/relationships/" + std::to_string( id ) ) };
					REQUIRE( lesser.status == drogon::k200OK );
					REQUIRE( lesser.json[ "superior" ].size() == 1 );
					CHECK( lesser.json[ "superior" ][ 0 ].asInt() == second_lesser );
					CHECK( lesser.json[ "inferior" ].empty() );
				}

				pqxx::nontransaction tx { db() };
				CHECK(
					tx.query_value< Integer >(
						"SELECT duplicate_id FROM duplicate_groups WHERE king_id = $1", pqxx::params { second_lesser } )
					== second_duplicate_id );
				CHECK( tx.query_value< Integer >( "SELECT count(*) FROM duplicate_groups" ) == 1 );
				CHECK( tx.query_value< Integer >( "SELECT count(*) FROM duplicate_group_inferiors" ) == 3 );
				CHECK_FALSE( tx.query_value< bool >(
					"SELECT EXISTS (SELECT 1 FROM duplicate_groups duplicate_group "
					"JOIN duplicate_group_inferiors inferior ON inferior.inferior_id = duplicate_group.king_id)" ) );
			}
		}
	}
}

static void seedPerceptualHash( pqxx::connection& connection, const RecordID record_id, const std::string& hex )
{
	pqxx::work tx { connection };
	tx.exec(
		"INSERT INTO image_metadata (record_id, width, height, channels, phash) VALUES ($1, 640, 480, 4, $2::bit(64))",
		pqxx::params { record_id, "x" + hex } );
	tx.commit();
}

SCENARIO_METHOD( ServerFixture, "Similar records are found by perceptual hash distance", "[api][relationships][phash]" )
{
	GIVEN( "records whose perceptual hashes sit at known distances apart" )
	{
		const auto records { api().createRecords( { 11, 12, 13, 14, 15 } ) };
		const auto probe { records[ 0 ] };
		const auto one_bit { records[ 1 ] };
		const auto three_bits { records[ 2 ] };
		const auto distant { records[ 3 ] };
		const auto unhashed { records[ 4 ] };

		seedPerceptualHash( db(), probe, "b44dc7b24dcb381c" );
		seedPerceptualHash( db(), one_bit, "b44dc7b24dcb381d" );
		seedPerceptualHash( db(), three_bits, "b44dc7b24dcb381b" );
		seedPerceptualHash( db(), distant, "deadbeefdeadbeef" );

		const auto similar { [ & ]( const RecordID id, const std::string& query )
			                 { return api().get( "/relationships/" + std::to_string( id ) + "/similar" + query ); } };

		WHEN( "a distance wide enough for both near neighbours is requested" )
		{
			const auto response { similar( probe, "?distance=3" ) };

			THEN( "they are returned nearest first, without the probe or the distant record" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 2 );
				CHECK( response.json[ "results" ][ 0 ][ "record_id" ].asInt() == one_bit );
				CHECK( response.json[ "results" ][ 0 ][ "distance" ].asInt() == 1 );
				CHECK( response.json[ "results" ][ 1 ][ "record_id" ].asInt() == three_bits );
				CHECK( response.json[ "results" ][ 1 ][ "distance" ].asInt() == 3 );
				CHECK_FALSE( response.json[ "truncated" ].asBool() );
			}
		}

		WHEN( "a tighter distance is requested" )
		{
			THEN( "only the neighbours within it are returned" )
			{
				const auto exact { similar( probe, "?distance=0" ) };
				REQUIRE( exact.status == drogon::k200OK );
				CHECK( exact.json[ "results" ].empty() );

				const auto near { similar( probe, "?distance=1" ) };
				REQUIRE( near.status == drogon::k200OK );
				REQUIRE( near.json[ "results" ].size() == 1 );
				CHECK( near.json[ "results" ][ 0 ][ "record_id" ].asInt() == one_bit );
			}
		}

		WHEN( "the search is run from a neighbour" )
		{
			const auto response { similar( one_bit, "?distance=4" ) };

			THEN( "distance is measured from that record instead" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 2 );
				CHECK( response.json[ "results" ][ 0 ][ "record_id" ].asInt() == probe );
				CHECK( response.json[ "results" ][ 0 ][ "distance" ].asInt() == 1 );
				CHECK( response.json[ "results" ][ 1 ][ "record_id" ].asInt() == three_bits );
				CHECK( response.json[ "results" ][ 1 ][ "distance" ].asInt() == 2 );
			}
		}

		WHEN( "the limit cuts the result short" )
		{
			const auto response { similar( probe, "?distance=64&limit=1" ) };

			THEN( "the truncation is reported" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 1 );
				CHECK( response.json[ "truncated" ].asBool() );
			}
		}

		WHEN( "the number of matches is exactly the limit" )
		{
			const auto response { similar( probe, "?distance=1&limit=1" ) };

			THEN( "the complete result is not reported as truncated" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 1 );
				CHECK_FALSE( response.json[ "truncated" ].asBool() );
			}
		}

		WHEN( "the request cannot be answered" )
		{
			THEN( "each failure is reported distinctly" )
			{
				CHECK( similar( unhashed, "?distance=3" ).status == drogon::k404NotFound );
				CHECK( similar( 999999, "?distance=3" ).status == drogon::k404NotFound );
				CHECK( similar( probe, "?distance=abc" ).status == drogon::k400BadRequest );
				CHECK( similar( probe, "?distance=65" ).status == drogon::k400BadRequest );
				CHECK( similar( probe, "?limit=99999" ).status == drogon::k400BadRequest );
			}
		}
	}
}

static Json::Value pairBody( const RecordID a, const RecordID b )
{
	Json::Value json {};
	json[ "record_id_a" ] = a;
	json[ "record_id_b" ] = b;
	return json;
}

SCENARIO_METHOD( ServerFixture, "Relationships between two records can be cleared", "[api][relationships]" )
{
	GIVEN( "a duplicate pair and three records paired as alternatives" )
	{
		const auto records { api().createRecords( { 21, 22, 23, 24, 25 } ) };
		const auto superior { records[ 0 ] };
		const auto inferior { records[ 1 ] };
		const auto first_alternative { records[ 2 ] };
		const auto second_alternative { records[ 3 ] };
		const auto third_alternative { records[ 4 ] };

		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( inferior, superior ) ).status
			== drogon::k200OK );

		Json::Value alternatives { Json::arrayValue };
		alternatives.append( first_alternative );
		alternatives.append( second_alternative );
		alternatives.append( third_alternative );
		REQUIRE( api().post( "/relationships/alternatives/add", alternatives ).status == drogon::k200OK );

		WHEN( "the duplicate pair is cleared" )
		{
			const auto response { api().post( "/relationships/clear", pairBody( superior, inferior ) ) };

			THEN( "the pair is gone in both directions" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK( response.json[ "duplicate_removed" ].asBool() );
				CHECK_FALSE( response.json[ "alternative_removed" ].asBool() );

				const auto after { api().get( "/relationships/" + std::to_string( superior ) ) };
				REQUIRE( after.status == drogon::k200OK );
				CHECK( after.json[ "inferior" ].empty() );
				CHECK( after.json[ "superior" ].empty() );

				pqxx::nontransaction tx { db() };
				CHECK_FALSE( tx.query_value< bool >(
					"SELECT EXISTS (SELECT 1 FROM duplicate_groups WHERE king_id = $1)", pqxx::params { superior } ) );
			}
		}

		WHEN( "one pair is cleared" )
		{
			const auto response {
				api().post( "/relationships/clear", pairBody( first_alternative, third_alternative ) )
			};

			THEN( "only that pair goes, and every other pair survives" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK( response.json[ "alternative_removed" ].asBool() );

				const auto after { api().get( "/relationships/" + std::to_string( first_alternative ) ) };
				REQUIRE( after.status == drogon::k200OK );
				REQUIRE( after.json[ "alternatives" ].size() == 1 );
				CHECK( after.json[ "alternatives" ][ 0 ].asInt() == second_alternative );

				const auto cleared { api().get( "/relationships/" + std::to_string( third_alternative ) ) };
				REQUIRE( cleared.status == drogon::k200OK );
				REQUIRE( cleared.json[ "alternatives" ].size() == 1 );
				CHECK( cleared.json[ "alternatives" ][ 0 ].asInt() == second_alternative );
			}
		}

		WHEN( "both of one record's pairs are cleared" )
		{
			REQUIRE(
				api().post( "/relationships/clear", pairBody( first_alternative, third_alternative ) ).status
				== drogon::k200OK );
			const auto response {
				api().post( "/relationships/clear", pairBody( first_alternative, second_alternative ) )
			};

			THEN( "it is left with none, while the pair it was not part of stands" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK( response.json[ "alternative_removed" ].asBool() );

				const auto after { api().get( "/relationships/" + std::to_string( first_alternative ) ) };
				REQUIRE( after.status == drogon::k200OK );
				CHECK( after.json[ "alternatives" ].empty() );

				const auto remaining { api().get( "/relationships/" + std::to_string( second_alternative ) ) };
				REQUIRE( remaining.status == drogon::k200OK );
				REQUIRE( remaining.json[ "alternatives" ].size() == 1 );
				CHECK( remaining.json[ "alternatives" ][ 0 ].asInt() == third_alternative );
			}
		}

		WHEN( "two unrelated records are cleared" )
		{
			const auto response { api().post( "/relationships/clear", pairBody( superior, first_alternative ) ) };

			THEN( "nothing is reported as removed" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK_FALSE( response.json[ "duplicate_removed" ].asBool() );
				CHECK_FALSE( response.json[ "alternative_removed" ].asBool() );
			}
		}

		WHEN( "the request is malformed" )
		{
			THEN( "it is rejected" )
			{
				CHECK(
					api().post( "/relationships/clear", pairBody( superior, superior ) ).status
					== drogon::k400BadRequest );
				CHECK(
					api().post( "/relationships/clear", pairBody( superior, 999999 ) ).status == drogon::k404NotFound );
			}
		}
	}
}

SCENARIO_METHOD( ServerFixture, "Alternatives do not spread between duplicates", "[api][relationships]" )
{
	GIVEN( "a duplicate pair whose halves are each an alternative of the same third record" )
	{
		const auto records { api().createRecords( { 51, 52, 53 } ) };
		const auto superior { records[ 0 ] };
		const auto inferior { records[ 1 ] };
		const auto shared { records[ 2 ] };

		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( inferior, superior ) ).status
			== drogon::k200OK );

		const auto pairAlternatives {
			[ & ]( const RecordID first, const RecordID second )
			{
				Json::Value alternatives { Json::arrayValue };
				alternatives.append( first );
				alternatives.append( second );
				return api().post( "/relationships/alternatives/add", alternatives ).status;
			}
		};

		REQUIRE( pairAlternatives( superior, shared ) == drogon::k200OK );
		REQUIRE( pairAlternatives( inferior, shared ) == drogon::k200OK );

		WHEN( "each half is asked for its relationships" )
		{
			const auto better { api().get( "/relationships/" + std::to_string( superior ) ) };
			const auto worse { api().get( "/relationships/" + std::to_string( inferior ) ) };

			THEN( "the shared record is an alternative of both, and the two stay duplicates of each other" )
			{
				REQUIRE( better.status == drogon::k200OK );
				REQUIRE( worse.status == drogon::k200OK );

				REQUIRE( better.json[ "alternatives" ].size() == 1 );
				CHECK( better.json[ "alternatives" ][ 0 ].asInt() == shared );
				REQUIRE( worse.json[ "alternatives" ].size() == 1 );
				CHECK( worse.json[ "alternatives" ][ 0 ].asInt() == shared );

				REQUIRE( better.json[ "inferior" ].size() == 1 );
				CHECK( better.json[ "inferior" ][ 0 ].asInt() == inferior );
				REQUIRE( worse.json[ "superior" ].size() == 1 );
				CHECK( worse.json[ "superior" ][ 0 ].asInt() == superior );
			}
		}

		WHEN( "the shared record is asked for its relationships" )
		{
			const auto response { api().get( "/relationships/" + std::to_string( shared ) ) };

			THEN( "it holds both pairs without either reaching the other" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "alternatives" ].size() == 2 );
				CHECK( response.json[ "alternatives" ][ 0 ].asInt() == std::min( superior, inferior ) );
				CHECK( response.json[ "alternatives" ][ 1 ].asInt() == std::max( superior, inferior ) );
			}
		}

		WHEN( "one of the two pairs is cleared" )
		{
			REQUIRE( api().post( "/relationships/clear", pairBody( inferior, shared ) ).status == drogon::k200OK );

			THEN( "the other is untouched" )
			{
				const auto worse { api().get( "/relationships/" + std::to_string( inferior ) ) };
				REQUIRE( worse.status == drogon::k200OK );
				CHECK( worse.json[ "alternatives" ].empty() );

				const auto better { api().get( "/relationships/" + std::to_string( superior ) ) };
				REQUIRE( better.status == drogon::k200OK );
				REQUIRE( better.json[ "alternatives" ].size() == 1 );
				CHECK( better.json[ "alternatives" ][ 0 ].asInt() == shared );
			}
		}
	}
}

SCENARIO_METHOD( ServerFixture, "Unrelated pairs drop out of distance searches", "[api][relationships][phash]" )
{
	GIVEN( "two records a single bit apart" )
	{
		const auto records { api().createRecords( { 31, 32, 33 } ) };
		const auto probe { records[ 0 ] };
		const auto lookalike { records[ 1 ] };
		const auto other { records[ 2 ] };

		seedPerceptualHash( db(), probe, "b44dc7b24dcb381c" );
		seedPerceptualHash( db(), lookalike, "b44dc7b24dcb381d" );
		seedPerceptualHash( db(), other, "b44dc7b24dcb381b" );

		const auto similar {
			[ & ]( const std::string& query )
			{ return api().get( "/relationships/" + std::to_string( probe ) + "/similar" + query ); }
		};

		REQUIRE( similar( "?distance=4" ).json[ "results" ].size() == 2 );

		WHEN( "one of them is marked unrelated" )
		{
			REQUIRE(
				api().post( "/relationships/unrelated/add", pairBody( probe, lookalike ) ).status == drogon::k200OK );

			THEN( "it stops being returned, and the other record is untouched" )
			{
				const auto response { similar( "?distance=4" ) };
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 1 );
				CHECK( response.json[ "results" ][ 0 ][ "record_id" ].asInt() == other );
				CHECK_FALSE( response.json[ "include_unrelated" ].asBool() );
			}

			THEN( "the flag brings it back, flagged as unrelated" )
			{
				const auto response { similar( "?distance=4&include_unrelated=true" ) };
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 2 );
				CHECK( response.json[ "include_unrelated" ].asBool() );

				for ( const auto& result : response.json[ "results" ] )
					CHECK( result[ "unrelated" ].asBool() == ( result[ "record_id" ].asInt() == lookalike ) );
			}

			THEN( "the mark holds when the search runs from the other side of the pair" )
			{
				const auto response {
					api().get( "/relationships/" + std::to_string( lookalike ) + "/similar?distance=4" )
				};
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 1 );
				CHECK( response.json[ "results" ][ 0 ][ "record_id" ].asInt() == other );
			}

			THEN( "marking it twice is not an error" )
			{
				CHECK(
					api().post( "/relationships/unrelated/add", pairBody( lookalike, probe ) ).status
					== drogon::k200OK );
				CHECK( similar( "?distance=4" ).json[ "results" ].size() == 1 );
			}

			THEN( "clearing the pair undoes it" )
			{
				const auto cleared { api().post( "/relationships/clear", pairBody( probe, lookalike ) ) };
				REQUIRE( cleared.status == drogon::k200OK );
				CHECK( cleared.json[ "unrelated_removed" ].asBool() );
				CHECK( similar( "?distance=4" ).json[ "results" ].size() == 2 );
			}
		}
	}
}

SCENARIO_METHOD( ServerFixture, "Decided pairs drop out of distance searches", "[api][relationships][phash]" )
{
	GIVEN( "three lookalikes, one already a duplicate and one already an alternative" )
	{
		const auto records { api().createRecords( { 41, 42, 43, 44 } ) };
		const auto probe { records[ 0 ] };
		const auto worse { records[ 1 ] };
		const auto alternative { records[ 2 ] };
		const auto undecided { records[ 3 ] };

		seedPerceptualHash( db(), probe, "b44dc7b24dcb381c" );
		seedPerceptualHash( db(), worse, "b44dc7b24dcb381d" );
		seedPerceptualHash( db(), alternative, "b44dc7b24dcb381e" );
		seedPerceptualHash( db(), undecided, "b44dc7b24dcb381b" );

		const auto similar {
			[ & ]( const std::string& query )
			{ return api().get( "/relationships/" + std::to_string( probe ) + "/similar" + query ); }
		};

		REQUIRE( similar( "?distance=4" ).json[ "results" ].size() == 3 );

		REQUIRE(
			api().post( "/relationships/duplicates/add", duplicateBody( worse, probe ) ).status == drogon::k200OK );

		Json::Value alternatives { Json::arrayValue };
		alternatives.append( probe );
		alternatives.append( alternative );
		REQUIRE( api().post( "/relationships/alternatives/add", alternatives ).status == drogon::k200OK );

		WHEN( "the search runs with its defaults" )
		{
			const auto response { similar( "?distance=4" ) };

			THEN( "only the undecided lookalike is left" )
			{
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 1 );
				CHECK( response.json[ "results" ][ 0 ][ "record_id" ].asInt() == undecided );
				CHECK_FALSE( response.json[ "include_related" ].asBool() );
			}
		}

		WHEN( "known relationships are asked for" )
		{
			const auto response { similar( "?distance=4&include_related=true" ) };

			THEN( "all three come back" )
			{
				REQUIRE( response.status == drogon::k200OK );
				CHECK( response.json[ "results" ].size() == 3 );
				CHECK( response.json[ "include_related" ].asBool() );
			}
		}

		WHEN( "a duplicate joins the group through a lesser member" )
		{
			const auto deep { api().createRecords( { 45 } )[ 0 ] };
			seedPerceptualHash( db(), deep, "b44dc7b24dcb3819" );
			REQUIRE(
				api().post( "/relationships/duplicates/add", duplicateBody( deep, worse ) ).status == drogon::k200OK );

			THEN( "it is filtered too, since the whole flat group is known" )
			{
				const auto response { similar( "?distance=4" ) };
				REQUIRE( response.status == drogon::k200OK );
				REQUIRE( response.json[ "results" ].size() == 1 );
				CHECK( response.json[ "results" ][ 0 ][ "record_id" ].asInt() == undecided );
			}
		}

		WHEN( "the last lookalike is marked unrelated as well" )
		{
			REQUIRE(
				api().post( "/relationships/unrelated/add", pairBody( probe, undecided ) ).status == drogon::k200OK );

			THEN( "the search empties out, and each flag reopens its own half" )
			{
				CHECK( similar( "?distance=4" ).json[ "results" ].empty() );
				CHECK( similar( "?distance=4&include_unrelated=true" ).json[ "results" ].size() == 1 );
				CHECK( similar( "?distance=4&include_related=true" ).json[ "results" ].size() == 2 );
				CHECK(
					similar( "?distance=4&include_related=true&include_unrelated=true" ).json[ "results" ].size()
					== 3 );
			}
		}
	}
}

SCENARIO_METHOD( ServerFixture, "Unknown file relationships are rejected", "[api][relationships]" )
{
	CHECK( api().get( "/relationships/999999" ).status == drogon::k404NotFound );
}

} // namespace idhan::test
