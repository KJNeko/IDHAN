#include <catch2/catch_test_macros.hpp>

#include "MimeIDs.hpp"
#include "ipc/Protocol.hpp"

namespace idhan::test
{

TEST_CASE( "Parser pHash capabilities survive manifests and affect worker signatures", "[modules][phash]" )
{
	ipc::ManifestEntry entry {
		.name = "metadata",
		.type = ModuleTypeFlags::METADATA,
		.mimes = { mime_ids::IMAGE_PNG, mime_ids::ANIMATION_GIF },
		.phash_mimes = { mime_ids::IMAGE_PNG }
	};
	const auto wire { ipc::toJson( entry ) };
	const auto decoded_e { ipc::manifestEntryFromJson( wire ) };
	REQUIRE( decoded_e.has_value() );
	CHECK( decoded_e.value().phash_mimes == entry.phash_mimes );
	const auto signature { ipc::manifestSignature( { entry } ) };
	entry.phash_mimes.clear();
	CHECK( ipc::manifestSignature( { entry } ) != signature );

	auto legacy { wire };
	legacy.removeMember( ipc::field::PHASH_MIMES );
	const auto legacy_e { ipc::manifestEntryFromJson( legacy ) };
	REQUIRE( legacy_e.has_value() );
	CHECK( legacy_e.value().phash_mimes.empty() );
}

TEST_CASE( "Manifest rejects invalid pHash capabilities", "[modules][phash]" )
{
	const ipc::ManifestEntry entry {
		.name = "metadata",
		.type = ModuleTypeFlags::METADATA,
		.mimes = { mime_ids::IMAGE_PNG },
		.phash_mimes = { mime_ids::IMAGE_PNG }
	};
	auto wire { ipc::toJson( entry ) };
	SECTION( "non-array" )
	{
		wire[ ipc::field::PHASH_MIMES ] = true;
	}
	SECTION( "non-integer" )
	{
		wire[ ipc::field::PHASH_MIMES ][ 0 ] = "png";
	}
	SECTION( "unsupported MIME" )
	{
		wire[ ipc::field::PHASH_MIMES ][ 0 ] = mime_ids::ANIMATION_GIF;
	}
	SECTION( "non-metadata module" )
	{
		wire[ ipc::field::TYPE ] = ModuleTypeFlags::THUMBNAILER;
	}
	CHECK_FALSE( ipc::manifestEntryFromJson( wire ).has_value() );
}

} // namespace idhan::test
