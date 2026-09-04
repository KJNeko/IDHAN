#include <catch2/catch_test_macros.hpp>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "MIME parsing uses the shared identification pipeline", "[api][mime]" )
{
	const std::string png_header { "\x89PNG\r\n\x1a\n", 8 };

	WHEN( "PNG bytes are parsed" )
	{
		const auto response { api().postOctets( "/mime/parse", png_header ) };

		THEN( "the specialized type is reported" )
		{
			REQUIRE( response.status == drogon::k200OK );
			CHECK( response.json[ "success" ].asBool() );
			CHECK( static_cast< MimeID >( response.json[ "mime_id" ].asInt() ) == mime_ids::IMAGE_PNG );
			CHECK(
				response.json[ "generic_mime_id" ].asUInt()
				== static_cast< Json::UInt >( std::to_underlying( SimpleMimeType::IMAGE_TYPE ) ) );
		}
	}

	WHEN( "PNG bytes carry a misleading CBZ extension" )
	{
		const auto response { api().postOctets( "/mime/parse", png_header, { { "filename", "image.cbz" } } ) };

		THEN( "the extension does not override the signature" )
		{
			REQUIRE( response.status == drogon::k200OK );
			CHECK( static_cast< MimeID >( response.json[ "mime_id" ].asInt() ) == mime_ids::IMAGE_PNG );
		}
	}

	WHEN( "ZIP bytes carry a CBZ extension" )
	{
		const std::string zip_header { "PK\x03\x04", 4 };
		const auto response { api().postOctets( "/mime/parse", zip_header, { { "filename", "comic.cbz" } } ) };

		THEN( "the archive module specializes it to CBZ" )
		{
			REQUIRE( response.status == drogon::k200OK );
			CHECK( static_cast< MimeID >( response.json[ "mime_id" ].asInt() ) == mime_ids::COMICBOOK_ZIP );
			CHECK(
				response.json[ "generic_mime_id" ].asUInt()
				== static_cast< Json::UInt >( std::to_underlying( SimpleMimeType::ARCHIVE ) ) );
		}
	}

	WHEN( "ZIP bytes carry a regular ZIP extension" )
	{
		const std::string zip_header { "PK\x03\x04", 4 };
		const auto response { api().postOctets( "/mime/parse", zip_header, { { "filename", "archive.zip" } } ) };

		THEN( "the signature remains a plain ZIP" )
		{
			REQUIRE( response.status == drogon::k200OK );
			CHECK( static_cast< MimeID >( response.json[ "mime_id" ].asInt() ) == mime_ids::APPLICATION_ZIP );
		}
	}
}

} // namespace idhan::test
