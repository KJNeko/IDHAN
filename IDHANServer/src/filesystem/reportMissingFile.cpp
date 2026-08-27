#include "filesystem.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

Task<> reportMissingFile( const RecordID record_id, const std::filesystem::path& expected_path, DbClientPtr db )
{
	// Missing-file side effects live here so notification delivery can be added without changing detectors.
	log::warn( "Expected file is missing: record={}, path={}", record_id, expected_path.string() );
	co_await db->execSqlCoro(
		"INSERT INTO missing_files (record_id) VALUES ($1) ON CONFLICT (record_id) DO NOTHING", record_id );
}

} // namespace idhan::filesystem
