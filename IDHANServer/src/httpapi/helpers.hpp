#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/response/Response.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::httpapi
{

[[nodiscard]] runtime::Task< std::expected< void, http::Response > > validateRecordIds(
	std::vector< RecordID > record_ids,
	db::DBContext& db );

} // namespace idhan::httpapi
