#include <string>

#include <catch2/catch_test_macros.hpp>

#include "IDHANTypes.hpp"
#include "MigratedSchema.hpp"

namespace idhan::test
{

//! What is asserted here is the shape of the table; tests/api/mime/pinnedIds.cpp asserts the rows
//! migration 307 seeds into it.
SCENARIO_METHOD( MigratedSchema, "Mime table shape", "[db][mime][schema]" )
{
	pqxx::nontransaction tx { connection() };
	tx.exec( "SET search_path = " + db::makeSearchPath( schema() ) );

	GIVEN( "a freshly migrated schema" )
	{
		WHEN( "the table is described" )
		{
			THEN( "no id points at another one" )
			{
				const auto result { tx.exec(
					"SELECT count(*) FROM information_schema.columns "
					"WHERE table_schema = $1 AND table_name = 'mime' AND column_name = 'base_mime_id'",
					pqxx::params { schema() } ) };
				REQUIRE( result[ 0 ][ 0 ].as< std::size_t >() == 0 );
			}
		}

		WHEN( "two rows share a name" )
		{
			tx.exec( "INSERT INTO mime (mime_id, name, best_extension) VALUES (9000, 'test/shared', 'a')" );

			THEN( "both are allowed, since a name is not an identity" )
			{
				REQUIRE_NOTHROW(
					tx.exec( "INSERT INTO mime (mime_id, name, best_extension) VALUES (9001, 'test/shared', 'b')" ) );
			}
		}

		WHEN( "a row is renumbered" )
		{
			tx.exec( "INSERT INTO mime (mime_id, name, best_extension) VALUES (9000, 'test/shared', 'a')" );

			const auto record { tx.exec( "INSERT INTO records (sha256) "
				                         "VALUES (decode(repeat('ab', 32), 'hex')) RETURNING record_id" ) };
			const auto record_id { record[ 0 ][ 0 ].as< RecordID >() };

			const auto cluster { tx.exec( "INSERT INTO file_clusters (folder_path) "
				                          "VALUES ('/tmp/idhan-mime-test') RETURNING cluster_id" ) };
			const auto cluster_id { cluster[ 0 ][ 0 ].as< ClusterID >() };

			tx.exec(
				"INSERT INTO file_info (size, record_id, mime_id, cluster_id) VALUES (1, $1, 9000, $2)",
				pqxx::params { record_id, cluster_id } );

			tx.exec( "UPDATE mime SET mime_id = 9500 WHERE mime_id = 9000" );

			THEN( "the referencing file follows it" )
			{
				const auto result { tx.exec(
					"SELECT mime_id FROM file_info WHERE record_id = $1", pqxx::params { record_id } ) };
				REQUIRE( result.size() == 1 );
				REQUIRE( result[ 0 ][ 0 ].as< MimeID >() == 9500 );
			}
		}
	}
}

} // namespace idhan::test
