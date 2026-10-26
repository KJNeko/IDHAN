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

// Returns every stored spelling with the same comparison key, preserving the original URL rows.
[[nodiscard]] runtime::Task< std::vector< UrlID > > findEquivalentUrls(
	std::string url,
	std::vector< std::string > ephemeral_query,
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
