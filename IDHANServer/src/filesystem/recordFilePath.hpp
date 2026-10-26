#pragma once

#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "VerifiedRecordPath.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::filesystem
{
// Resolves and pins the assigned cluster without checking the file or changing missing-file state.
// An absent value means the record has no assigned cluster; an error means identity verification failed.
[[nodiscard]] runtime::Task< std::expected< std::optional< VerifiedRecordPath >, std::string > > findRecordFilePath(
	db::DBContext& db,
	RecordID record_id );

// Confirms a regular file on a verified cluster. Confirmed absence records missing-file state;
// an inaccessible or unverifiable cluster is an error and does not change the assignment.
[[nodiscard]] runtime::Task< std::expected< std::optional< VerifiedRecordPath >, std::string > >
	findPresentRecordFilePath( db::DBContext& db, RecordID record_id );
} // namespace idhan::filesystem
