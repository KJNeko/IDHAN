#include "filesystem.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

Task<> updateRecordModifiedTime( const RecordID record_id, const std::filesystem::path& path, const DbClientPtr db )
{
	const auto modified_time { getLastWriteTime( path ) };
	const auto modified_time_us {
		std::chrono::duration_cast< std::chrono::microseconds >( modified_time.time_since_epoch() ).count()
	};

	log::trace(
		"Updating modified time for Record {} to {}", record_id, format_ns::format( "{:%F %T}", modified_time ) );

	co_await db->execSqlCoro(
		"UPDATE file_info SET modified_time = TIMESTAMP 'epoch' + $1::bigint * INTERVAL '1 microsecond' WHERE record_id = $2",
		modified_time_us,
		record_id );
}

ExpectedTask< void > updateRecordModifiedTime( const RecordID record_id, DbClientPtr db )
{
	const auto path { co_await getRecordPath( record_id, db ) };
	return_unexpected_error( path );

	co_await updateRecordModifiedTime( record_id, *path, db );
	co_return {};
}

} // namespace idhan::filesystem
