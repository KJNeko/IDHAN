#include "MetadataInfo.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "metadata.hpp"

namespace idhan::metadata
{

drogon::Task< MetadataInfo > getMetadata( [[maybe_unused]] const RecordID record_id, [[maybe_unused]] DbClientPtr db )
{
	idhan::MetadataInfo metadata {};

	const auto metadata_info { co_await db->execSqlCoro( "SELECT * FROM metadata WHERE record_id = $1", record_id ) };

	if ( metadata_info.empty() )
	{
		co_return {};
	}

	metadata.m_extra = metadata_info[ 0 ][ "json" ].as< Json::Value >();
	metadata.m_simple_type =
		static_cast< SimpleMimeType >( metadata_info[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() );

	switch ( metadata.m_simple_type )
	{
		case SimpleMimeType::NONE:
			break;
		case SimpleMimeType::IMAGE_TYPE:
			break;
		case SimpleMimeType::VIDEO:
			break;
		case SimpleMimeType::ANIMATION:
			break;
		case SimpleMimeType::AUDIO:
			break;
		case SimpleMimeType::ARCHIVE:
			{
				const auto archive_metadata_info {
					co_await db->execSqlCoro( "SELECT * FROM archive_metadata WHERE record_id = $1", record_id )
				};

				idhan::MetadataInfoArchive metadata_info_archive {};
				metadata_info_archive.encrypted = archive_metadata_info[ 0 ][ "encrypted" ].as< bool >();

				// metadata_info_archive.m_size = archive_metadata_info[0][""];

				const auto archive_metadata_groups { co_await db->execSqlCoro(
					"SELECT sha256 FROM archive_map JOIN records USING (record_id) WHERE archive_id = $1",
					archive_metadata_info[ 0 ][ "archive_id" ].as< ArchiveID >() ) };

				for ( const auto& row : archive_metadata_groups )
				{
					const SHA256 sha256 { SHA256::fromPgCol( row[ "sha256" ] ) };
					metadata_info_archive.contained_hashes.emplace_back( sha256.data() );
				}
				break;
			}
		case SimpleMimeType::IMAGE_PROJECT:
			break;
		default:
			break;
	}

	co_return metadata;
}

} // namespace idhan::metadata