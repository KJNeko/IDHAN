#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

static RecordIDs sortedRecordIds( const ApiResponse& response )
{
	REQUIRE( response.status == 200 );
	const auto& values { response.json.isArray() ? response.json : response.json[ "record_ids" ] };
	REQUIRE( values.isArray() );
	RecordIDs ids;
	for ( const auto& value : values ) ids.push_back( value.asInt() );
	return ids;
}

static ApiResponse postSort( ApiClient& api, const std::string_view method, const std::string_view order )
{
	Json::Value body {};
	body[ "sort" ][ "by" ] = std::string { method };
	body[ "sort" ][ "order" ] = std::string { order };
	return api.post( "/search", body );
}

class SearchSortingFixture : public ServerFixture
{
  protected:

	RecordIDs records { api().createRecords( { 2201, 2202, 2203 } ) };

  public:

	SearchSortingFixture()
	{
		const std::array< std::int32_t, 3 > sizes { 30, 10, 20 };
		const std::array< std::int32_t, 3 > mtimes { 2, 1, 3 };
		const std::array< std::int32_t, 3 > ctimes { 1, 3, 2 };
		const std::array< std::int32_t, 3 > creation_times { 2, 3, 1 };
		const std::array< MimeID, 3 > mimes { mime_ids::ANIMATION_GIF, mime_ids::IMAGE_PNG, mime_ids::IMAGE_JPEG };
		pqxx::work tx { db() };
		for ( std::size_t index = 0; index < records.size(); ++index )
		{
			tx.exec(
				"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time, file_mtime, file_ctime) "
				"VALUES ($1, $2, $3, TIMESTAMP '1970-01-01' + $4::integer * INTERVAL '1 second', "
				"TIMESTAMP '1970-01-01' + $5::integer * INTERVAL '1 second', "
				"TIMESTAMP '1970-01-01' + $6::integer * INTERVAL '1 second')",
				pqxx::params {
					records[ index ],
					sizes[ index ],
					mimes[ index ],
					static_cast< std::int32_t >( index + 1 ),
					mtimes[ index ],
					ctimes[ index ] } );
			tx.exec(
				"UPDATE records SET creation_time = TIMESTAMP '1970-01-01' + $2::integer * INTERVAL '1 second' "
				"WHERE record_id = $1",
				pqxx::params { records[ index ], creation_times[ index ] } );
		}
		tx.commit();
	}
};

TEST_CASE_METHOD(
	ServerFixture,
	"Search rejects unknown sorting methods and invalid method types",
	"[api][search][sorting]" )
{
	for ( const auto method : { "duraton", "", "unknown", "IMPORT_TIME", " size " } )
	{
		CAPTURE( method );
		const auto get { api().get( "/search", { { "by", method } } ) };
		CHECK( get.status == 400 );
		CHECK( get.body.find( "Unknown sorting method" ) != std::string::npos );
		const auto post { postSort( api(), method, "asc" ) };
		CHECK( post.status == 400 );
		CHECK( post.body.find( "Unknown sorting method" ) != std::string::npos );
	}

	const std::array< Json::Value, 5 > invalid_types {
		Json::Value {},
		Json::Value { false },
		Json::Value { 42 },
		Json::Value { Json::arrayValue },
		Json::Value { Json::objectValue }
	};
	for ( const auto& method : invalid_types )
	{
		Json::Value body {};
		body[ "sort" ][ "by" ] = method;
		const auto response { api().post( "/search", body ) };
		CHECK( response.status == 400 );
		CHECK( response.body.find( "'sort.by' must be a string" ) != std::string::npos );
	}
}

TEST_CASE_METHOD(
	SearchSortingFixture,
	"Omitted sorting methods preserve the GET and POST defaults",
	"[api][search][sorting]" )
{
	CHECK( sortedRecordIds( api().get( "/search" ) ) == RecordIDs { records[ 1 ], records[ 2 ], records[ 0 ] } );
	CHECK(
		sortedRecordIds( api().get( "/search", { { "order", "desc" } } ) )
		== RecordIDs { records[ 0 ], records[ 2 ], records[ 1 ] } );
	CHECK( sortedRecordIds( api().get( "/search", { { "by", "import_time" } } ) ) == records );

	Json::Value body { Json::objectValue };
	const RecordIDs newest_first { records[ 2 ], records[ 1 ], records[ 0 ] };
	CHECK( sortedRecordIds( api().post( "/search", body ) ) == newest_first );
	body[ "sort" ] = Json::Value { Json::objectValue };
	CHECK( sortedRecordIds( api().post( "/search", body ) ) == newest_first );
	body[ "sort" ][ "order" ] = "asc";
	CHECK( sortedRecordIds( api().post( "/search", body ) ) == records );
	body[ "sort" ].removeMember( "order" );
	body[ "sort" ][ "by" ] = "filesize";
	CHECK( sortedRecordIds( api().post( "/search", body ) ) == RecordIDs { records[ 0 ], records[ 2 ], records[ 1 ] } );
}

TEST_CASE_METHOD(
	SearchSortingFixture,
	"Supported sort names and aliases work in both search transports",
	"[api][search][sorting]" )
{
	const std::array alias_groups {
		std::pair { std::vector< std::string_view > { "filesize", "size" },
		            RecordIDs { records[ 1 ], records[ 2 ], records[ 0 ] } },
		std::pair { std::vector< std::string_view > { "record_creation_time", "record_time", "creation_time" },
		            RecordIDs { records[ 2 ], records[ 0 ], records[ 1 ] } },
		std::pair { std::vector< std::string_view > { "file_modified_time", "file_mtime", "modified_time" },
		            RecordIDs { records[ 1 ], records[ 0 ], records[ 2 ] } },
		std::pair { std::vector< std::string_view > { "file_created_time", "file_ctime" },
		            RecordIDs { records[ 0 ], records[ 2 ], records[ 1 ] } },
		std::pair { std::vector< std::string_view > { "mime", "filetype" },
		            RecordIDs { records[ 2 ], records[ 1 ], records[ 0 ] } }
	};
	for ( const auto& [ methods, ascending ] : alias_groups )
	{
		for ( const auto method : methods )
		{
			for ( const auto order : { "asc", "desc" } )
			{
				CAPTURE( method, order );
				auto expected { ascending };
				if ( std::string_view { order } == "desc" ) std::reverse( expected.begin(), expected.end() );
				CHECK(
					sortedRecordIds( api().get( "/search", { { "by", std::string { method } }, { "order", order } } ) )
					== expected );
				CHECK( sortedRecordIds( postSort( api(), method, order ) ) == expected );
			}
		}
	}

	for ( const auto method :
	      { "import_time",
	        "hash",
	        "random",
	        "duration",
	        "framerate",
	        "has_audio",
	        "width",
	        "height",
	        "ratio",
	        "num_pixels",
	        "num_tags" } )
	{
		for ( const auto order : { "asc", "desc" } )
		{
			CAPTURE( method, order );
			CHECK( api().get( "/search", { { "by", method }, { "order", order } } ).status == 200 );
			CHECK( postSort( api(), method, order ).status == 200 );
		}
	}
}

} // namespace idhan::test
