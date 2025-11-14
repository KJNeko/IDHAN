//
// Created by kj16609 on 10/30/25.
//

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "filesystem.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::filesystem
{
ExpectedTask< bool > checkFileExists( const RecordID record_id, DbClientPtr db )
{
	const auto file_path_e { co_await getRecordPath( record_id, db ) };
	return_unexpected_error( file_path_e );

	co_return std::filesystem::exists( *file_path_e );
}
} // namespace idhan::filesystem
