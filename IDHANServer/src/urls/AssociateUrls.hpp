#pragma once

#include <expected>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::helpers
{

//! Creates any missing domains and URLs, then maps each URL onto the record. Idempotent.
[[nodiscard]] runtime::Task< std::expected< void, std::string > > associateUrls(
	RecordID record_id,
	std::vector< std::string > urls,
	db::DBContext& db );

} // namespace idhan::helpers
