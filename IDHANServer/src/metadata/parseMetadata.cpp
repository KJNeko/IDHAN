//
// Created by kj16609 on 6/12/25.
//

#include <drogon/drogon.h>

#include "../filesystem/clusters/ClusterManager.hpp"
#include "../filesystem/io/IOUring.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "filesystem/filesystem.hpp"
#include "metadata.hpp"
#include "modules/ModuleLoader.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::metadata
{

ExpectedTask< MetadataInfo > parseMetadata( const RecordID record_id, DbClientPtr db )
{
	auto io { co_await filesystem::getIOForRecord( record_id, db ) };
	return_unexpected_error( io );

	const auto [ data, length ] = io->mmapReadOnly();

	const auto record_mime {
		co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
	};

	if ( record_mime.empty() )
		co_return std::unexpected( createBadRequest(
			"Record {} does not exist or does not have any file info associated with it", record_id ) );

	if ( record_mime[ 0 ][ "mime_id" ].isNull() ) co_return MetadataInfo {};

	const auto mime_id { record_mime[ 0 ][ "mime_id" ].as< MimeID >() };

	const auto mime_info { co_await db->execSqlCoro( "SELECT * FROM mime WHERE mime_id = $1", mime_id ) };

	if ( mime_info.empty() )
		co_return std::unexpected( createInternalError( "Unable to get mime info for mime_id {}", mime_id ) );

	const auto mime_name { mime_info[ 0 ][ "name" ].as< std::string >() };

	const std::shared_ptr< MetadataModuleI > parser { co_await findBestParser( mime_name ) };

	if ( parser == nullptr )
		co_return std::unexpected( createBadRequest( "No parser found for mime type {}", mime_name ) );

	const auto metadata { parser->parseFile( data, length, mime_name ) };

	if ( !metadata )
	{
		const ModuleError error { metadata.error() };
		auto ret { createInternalError( "Module failed to parse data for record {}: {}", record_id, error ) };
		co_return std::unexpected( ret );
	}

	co_return metadata.value();
}

} // namespace idhan::metadata
