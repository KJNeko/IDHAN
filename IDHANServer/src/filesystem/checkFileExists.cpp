#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "filesystem.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::filesystem
{
ExpectedTask< bool > checkFileExists( const RecordID record_id, DbClientPtr db )
{
	const auto file_path_e { co_await getRecordPath( record_id, db ) };
	if ( !file_path_e )
	{
		const auto missing {
			co_await db->execSqlCoro( "SELECT 1 FROM missing_files WHERE record_id = $1", record_id )
		};
		if ( !missing.empty() ) co_return false;

		co_return std::unexpected( file_path_e.error() );
	}

	co_return true;
}
} // namespace idhan::filesystem
