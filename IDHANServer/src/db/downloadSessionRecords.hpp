#pragma once

#include <optional>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;

struct RecordedSessionImport
{
	bool first_in_session {};
	std::int64_t total_imported_count {};
};

//! The parser's source-post timestamp is Unix seconds, independent of import time.
//! Counts new files once per session; known files still retain session membership.
[[nodiscard]] runtime::Task< RecordedSessionImport > recordDownloadSessionImport(
	DBContext& db,
	DownloadSessionID session_id,
	RecordID record_id,
	std::optional< std::int64_t > posted_at,
	const bool imported );
} // namespace idhan::db
