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

// Every URL passed to these helpers must already come from normalizeUrls.

[[nodiscard]] runtime::Task< std::expected< void, std::string > > setUrlTypes(
	std::vector< std::pair< std::string, std::string > > types,
	db::DBContext& db );

// Creates the URL and domain if necessary, then records its PostgreSQL UrlType value.
[[nodiscard]] runtime::Task< std::expected< void, std::string > > setUrlType(
	std::string url,
	std::string type,
	db::DBContext& db );

//! Creates any missing domains and URLs, then maps each URL onto the record. Idempotent.
[[nodiscard]] runtime::Task< std::expected< void, std::string > > associateUrls(
	RecordID record_id,
	std::vector< std::string > urls,
	db::DBContext& db );

} // namespace idhan::helpers
