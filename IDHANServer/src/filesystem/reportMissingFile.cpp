#include "filesystem.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

Task<> reportMissingFile( const RecordID record_id, const std::filesystem::path& expected_path, DbClientPtr db )
{
	// Missing-file side effects live here so notification delivery can be added without changing detectors.
	log::warn( "Expected file is missing: record={}, path={}", record_id, expected_path.string() );
	co_await db->execSqlCoro(
		"UPDATE file_info SET cluster_id = NULL WHERE record_id = $1 AND cluster_delete_time IS NULL", record_id );
}

} // namespace idhan::filesystem
