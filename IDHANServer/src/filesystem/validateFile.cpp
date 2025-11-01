//
// Created by kj16609 on 10/30/25.
//

#include "api/helpers/ExpectedTask.hpp"
#include "utility.hpp"

namespace idhan::filesystem
{

ExpectedTask< FileState > validateFile( const RecordID record_id, DbClientPtr db )
{
	const auto file_exists_e { co_await checkFileExists( record_id, db ) };
	return_unexpected_error( file_exists_e );

	if ( *file_exists_e ) co_return FileState::FileNotFound;

	// TODO: Validate hash
}

} // namespace idhan::filesystem
