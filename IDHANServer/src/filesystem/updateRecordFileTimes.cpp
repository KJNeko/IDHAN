#include "filesystem.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

Task<> updateRecordFileTimes( const RecordID record_id, const std::filesystem::path& path, const DbClientPtr db )
{
	const auto times { getFileTimes( path ) };

	if ( !times )
	{
		log::warn( "Failed to stat {} while updating the file times of Record {}", path.string(), record_id );
		co_return;
	}

	const std::int64_t mtime_us { toMicroseconds( times->mtime ) };
	const std::optional< std::int64_t > ctime_us { toMicroseconds( times->btime ) };

	log::trace(
		"Updating file times for Record {} to mtime {}", record_id, format_ns::format( "{:%F %T}", times->mtime ) );

	co_await db->execSqlCoro(
		"UPDATE file_info SET file_mtime = TIMESTAMP 'epoch' + $1::bigint * INTERVAL '1 microsecond', "
		"file_ctime = TIMESTAMP 'epoch' + $2::bigint * INTERVAL '1 microsecond' WHERE record_id = $3",
		mtime_us,
		ctime_us,
		record_id );
}

ExpectedTask< void > updateRecordFileTimes( const RecordID record_id, DbClientPtr db )
{
	const auto path { co_await getRecordPath( record_id, db ) };
	return_unexpected_error( path );

	co_await updateRecordFileTimes( record_id, *path, db );
	co_return {};
}

} // namespace idhan::filesystem
