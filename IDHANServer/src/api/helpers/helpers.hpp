#pragma once

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "db/drogonArrayBind.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::api::helpers
{

[[nodiscard]] std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainIDParameter(
	const drogon::HttpRequestPtr& request );

//! 404 if the tag domain or any referenced tag does not exist.
[[nodiscard]] ExpectedTask< void > validateRelationshipIds(
	TagDomainID tag_domain_id,
	std::vector< TagID > tag_ids,
	DbClientPtr db );

//! 404 if any referenced record does not exist.
[[nodiscard]] ExpectedTask< void > validateRecordIds( std::vector< RecordID > record_ids, DbClientPtr db );

//! 400 unless the parameter is absent, or an unsigned integer no greater than `maximum`.
[[nodiscard]] std::expected< std::uint16_t, drogon::HttpResponsePtr > parseBoundedParameter(
	const std::optional< std::string >& parameter,
	std::string_view name,
	std::uint16_t fallback,
	std::uint16_t maximum );

constexpr std::chrono::seconds default_max_age {
	std::chrono::duration_cast< std::chrono::seconds >( std::chrono::years( 1 ) )
};

void addFileCacheHeader( drogon::HttpResponsePtr sharedPtr, std::chrono::seconds max_age = default_max_age );

} // namespace idhan::api::helpers
