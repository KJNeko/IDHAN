#include <catch2/catch_test_macros.hpp>

#include "MigratedSchema.hpp"

namespace idhan::test
{

class ImageMetadataSchema : public MigratedSchema
{
  public:

	pqxx::connection& db() { return connection(); }
};

SCENARIO_METHOD( ImageMetadataSchema, "Image perceptual hashes have an exact database width", "[db][phash]" )
{
	int record_id { 0 };
	{
		pqxx::work tx { db() };
		record_id = tx.query_value< int >(
			"INSERT INTO records (sha256) VALUES (decode(repeat('00', 32), 'hex')) RETURNING record_id" );
		tx.exec(
			"INSERT INTO image_metadata (record_id, width, height, channels, phash) "
			"VALUES ($1, 1, 1, 3, 'xb44dc7b24dcb381c')",
			pqxx::params { record_id } );
		CHECK(
			tx.query_value< int >(
				"SELECT length(phash) FROM image_metadata WHERE record_id = $1", pqxx::params { record_id } )
			== 64 );
		tx.commit();
	}

	{
		pqxx::work tx { db() };
		CHECK_THROWS(
			tx.exec( "UPDATE image_metadata SET phash = 'xabcd' WHERE record_id = $1", pqxx::params { record_id } ) );
	}
}

} // namespace idhan::test
