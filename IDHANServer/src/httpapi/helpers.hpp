#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/Response.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::httpapi
{

[[nodiscard]] runtime::Task< std::expected< void, http::Response > > validateRecordIds(
	std::vector< RecordID > record_ids,
	db::DBContext& db );

[[nodiscard]] std::expected< std::uint16_t, http::Response > parseBoundedParameter(
	const std::optional< std::string >& parameter,
	std::string_view name,
	std::uint16_t fallback,
	std::uint16_t maximum );

} // namespace idhan::httpapi
