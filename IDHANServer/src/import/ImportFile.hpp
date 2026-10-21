#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "IDHANTypes.hpp"
#include "codes/ImportCodes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::db
{
class DBContext;
}

namespace idhan::imports
{

//! Carries the status the route layer should answer with, so that sharing this logic with the
//! downloader does not flatten the 400s and 500s it used to return into a single code.
struct ImportError
{
	std::uint16_t status {};
	std::string message {};
};

struct ImportOutcome
{
	ImportStatus status { Failed };
	RecordID record_id {};
	ImportFailureCode failure { UnknownReason };
	std::optional< std::int64_t > deleted_time {};
};

//! Imports already-buffered file bytes: dedupes on the hash, creates the record, writes the file
//! into a cluster and scans its metadata. Shared by the /file/import route and the downloader.
[[nodiscard]] runtime::Task< std::expected< ImportOutcome, ImportError > > importFile(
	std::span< const std::byte > data,
	const SHA256& sha256,
	std::string filename,
	bool force_import,
	db::DBContext& db );

} // namespace idhan::imports
