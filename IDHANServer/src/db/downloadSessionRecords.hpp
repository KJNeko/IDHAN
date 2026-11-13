#pragma once

#include <optional>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;

//! The parser's source-post timestamp is Unix seconds, independent of import time.
//! Returns whether this record was first added to the session.
[[nodiscard]] runtime::Task< bool > recordDownloadSessionImport(
	DBContext& db,
	DownloadSessionID session_id,
	RecordID record_id,
	std::optional< std::int64_t > posted_at );
} // namespace idhan::db
