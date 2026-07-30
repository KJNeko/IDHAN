//
// Created by kj16609 on 11/13/25.
//

#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/dbTypes.hpp"
#include "filesystem.hpp"
#include "io/IOUring.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::filesystem
{

ExpectedTask< FileIOUring > getIOForRecord( const RecordID record_id, DbClientPtr db )
{
	const auto path { co_await filesystem::getRecordPath( record_id, db ) };
	return_unexpected_error( path );

	if ( !std::filesystem::exists( *path ) )
	{
		co_return std::unexpected(
			createInternalError( "Record {} does not exist at the expected path \'{}\'.", record_id, path->string() ) );
	}

	FileIOUring uring { *path };
	co_return std::move( uring );
}

ExpectedTask< ipc::Blob > mapRecordBlob( const RecordID record_id, DbClientPtr db )
{
	const auto path { co_await filesystem::getRecordPath( record_id, db ) };
	return_unexpected_error( path );

	if ( !std::filesystem::exists( *path ) )
	{
		co_return std::unexpected(
			createInternalError( "Record {} does not exist at the expected path \'{}\'.", record_id, path->string() ) );
	}

	// The copy into the blob happens inside the kernel, so the file's bytes never land in this
	// process's heap the way readAll() put them there. The path stops here: what crosses to the
	// module is an anonymous descriptor that carries the bytes and nothing about where they live.
	auto blob { ipc::Blob::fromFile( *path ) };

	if ( !blob )
		co_return std::unexpected(
			createInternalError( "Failed to map file for record {}: {}", record_id, blob.error() ) );

	co_return std::move( *blob );
}

} // namespace idhan::filesystem