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
			createInternalError( "Record {} does not exist at the expected path {}.", record_id, path->string() ) );
	}

	FileIOUring uring { *path };
	co_return uring;
}

} // namespace idhan::filesystem