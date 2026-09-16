#pragma once

#include <expected>
#include <optional>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/RouteSpec.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] std::vector< http::RouteDefinition > recordRoutes( db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< RecordID, http::Response > > createRecord(
	const SHA256& sha256,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, db::DBContext& db );

} // namespace idhan::httpapi
