#pragma once

#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader
{
//! Distinct records in the session's current URL tree; imported takes precedence over already present.
struct SessionResult
{
	std::int64_t imported_count {};
	std::int64_t known_count {};
	std::optional< std::string > error {};
	std::int64_t finished_at {};
	std::int64_t total_imported_count {};
};

//! Called by the session observer after its parser/import callbacks have drained.
[[nodiscard]] runtime::Task< std::optional< SessionResult > > reportSessionResult(
	db::DBContext& db,
	DownloadSessionID session_id );
[[nodiscard]] runtime::Task< std::optional< SessionResult > > sessionResult(
	db::DBContext& db,
	DownloadSessionID session_id );
runtime::Task<> clearSessionResult( db::DBContext& db, DownloadSessionID session_id );

//! Retry the saved session roots using its saved settings. URL bookkeeping stays in the downloader.
[[nodiscard]] runtime::Task< std::expected< void, std::string > > retrySession(
	db::DBContext& db,
	DownloadSessionID session_id );
} // namespace idhan::downloader
