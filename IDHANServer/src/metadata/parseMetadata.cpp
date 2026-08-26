#include <drogon/drogon.h>

#include <vector>

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "core/files/mime.hpp"
#include "filesystem/filesystem.hpp"
#include "metadata.hpp"
#include "modules/ModuleLoader.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::metadata
{

ExpectedTask< MetadataInfo > parseMetadata(
	const RecordID record_id,
	const MimeID mime_id,
	std::shared_ptr< const modules::CallInput > input )
{
	const std::shared_ptr< modules::RemoteModule > parser { co_await findBestParser( mime_id ) };

	if ( parser == nullptr ) co_return std::unexpected( createBadRequest( "No parser found for mime id {}", mime_id ) );

	const modules::RemoteCallData call_data {
		.input = std::move( input ), .mime_id = mime_id, .extra = {}, .depth = 0
	};
	const auto metadata { co_await parser->parseFile( call_data ) };

	if ( !metadata )
	{
		const ModuleError error { metadata.error() };
		auto ret { createInternalError( "Module failed to parse data for record {}: {}", record_id, error ) };
		co_return std::unexpected( ret );
	}

	co_return metadata.value();
}

ExpectedTask< MetadataInfo > parseMetadata( const RecordID record_id, DbClientPtr db )
{
	log::debug( "Processing metadata for {}", record_id );

	const auto record_mime { co_await mime::lookupRecordMimeID( record_id, db ) };

	if ( !record_mime.has_file_info )
		co_return std::unexpected( createBadRequest(
			"Record {} does not exist or does not have any file info associated with it", record_id ) );

	if ( !record_mime.mime_id )
	{
		log::warn(
			"When trying to parse file for record {} for metadata, there was no mime associated with it", record_id );
		co_return std::unexpected( createBadRequest(
			"Record {} does not have any mime associated with it, Cannot parse metadata", record_id ) );
	}

	auto input { co_await filesystem::openRecordInput( record_id, db ) };
	return_unexpected_error( input );

	co_return co_await parseMetadata( record_id, *record_mime.mime_id, std::move( *input ) );
}

} // namespace idhan::metadata
