#pragma once

#include <span>
#include <string>

#include "IDHANTypes.hpp"
#include "codes/ImportCodes.hpp"
#include "db/dbTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::imports
{
struct ImportFileResult
{
	RecordID record_id {};
	ImportStatus status { ImportStatus::Failed };
	std::int64_t deleted_at {};
};

[[nodiscard]] ExpectedTask< ImportFileResult > importFile(
	std::span< const std::byte > data,
	std::string filename,
	bool force_import,
	DbClientPtr db );
} // namespace idhan::imports
