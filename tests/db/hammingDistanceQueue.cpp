#include <catch2/catch_test_macros.hpp>

#include <format>

#include "MigratedSchema.hpp"

namespace idhan::test
{

class HammingQueueSchema : public MigratedSchema
{
  public:

	pqxx::connection& db() { return connection(); }
};

static int seedRecord( pqxx::connection& connection, const int seed )
{
	pqxx::work tx { connection };
	const auto record_id { tx.query_value< int >(
		"INSERT INTO records (sha256) VALUES (decode(repeat($1, 32), 'hex')) RETURNING record_id",
		pqxx::params { std::format( "{:02x}", seed ) } ) };
	tx.commit();
	return record_id;
}

static bool queued( pqxx::connection& connection, const int record_id )
{
	pqxx::nontransaction tx { connection };
	return tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM hamming_distance_queue WHERE record_id = $1)", pqxx::params { record_id } );
}

static void drain( pqxx::connection& connection )
{
	pqxx::work tx { connection };
	tx.exec( "DELETE FROM hamming_distance_queue" );
	tx.commit();
}

SCENARIO_METHOD( HammingQueueSchema, "Gaining a perceptual hash queues a record for distances", "[db][phash]" )
{
	GIVEN( "image rows inserted with and without a hash" )
	{
		const auto hashed { seedRecord( db(), 1 ) };
		const auto unhashed { seedRecord( db(), 2 ) };

		{
			pqxx::work tx { db() };
			tx.exec(
				"INSERT INTO image_metadata (record_id, width, height, channels, phash) "
				"VALUES ($1, 1, 1, 3, 'xb44dc7b24dcb381c')",
				pqxx::params { hashed } );
			tx.exec(
				"INSERT INTO image_metadata (record_id, width, height, channels) VALUES ($1, 1, 1, 3)",
				pqxx::params { unhashed } );
			tx.commit();
		}

		THEN( "only the hashed one is queued" )
		{
			CHECK( queued( db(), hashed ) );
			CHECK_FALSE( queued( db(), unhashed ) );
		}

		WHEN( "a hash is written over the null, then replaced by a different one" )
		{
			drain( db() );

			{
				pqxx::work tx { db() };
				tx.exec(
					"UPDATE image_metadata SET phash = 'xb44dc7b24dcb381c' WHERE record_id = $1",
					pqxx::params { unhashed } );
				tx.commit();
			}

			THEN( "the null to populated write queues it" )
			{
				CHECK( queued( db(), unhashed ) );
			}

			drain( db() );

			{
				pqxx::work tx { db() };
				tx.exec(
					"UPDATE image_metadata SET phash = 'x0000000000000000' WHERE record_id = $1",
					pqxx::params { unhashed } );
				tx.exec( "UPDATE image_metadata SET width = 800 WHERE record_id = $1", pqxx::params { hashed } );
				tx.commit();
			}

			THEN( "neither a replaced hash nor an unrelated column change queues anything" )
			{
				CHECK_FALSE( queued( db(), unhashed ) );
				CHECK_FALSE( queued( db(), hashed ) );
			}
		}
	}
}

} // namespace idhan::test
