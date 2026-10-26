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

//! Creates the domain and any missing tags, then maps them onto the record. Idempotent.
[[nodiscard]] runtime::Task< std::expected< void, std::string > > associateTags(
	RecordID record_id,
	std::vector< std::string > tags,
	std::string tag_domain,
	db::DBContext& db );

} // namespace idhan::helpers
