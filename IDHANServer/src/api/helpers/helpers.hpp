//
// Created by kj16609 on 3/11/25.
//
#pragma once

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>

#include <expected>
#include <vector>

#include "db/dbTypes.hpp"
#include "db/drogonArrayBind.hpp"
#include "IDHANTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::api::helpers
{

[[nodiscard]] std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainIDParameter( const drogon::HttpRequestPtr& request );

//! 404 if the tag domain or any referenced tag does not exist. Used by the tag relationship
//! endpoints: the PG backend reports FK violations as a bare Failure, which would surface as 500s
[[nodiscard]] ExpectedTask< void > validateRelationshipIds(
	TagDomainID tag_domain_id,
	std::vector< TagID > tag_ids,
	DbClientPtr db );

//! 404 if any referenced record does not exist. Used by the record mutation endpoints
//! for the same reason as validateRelationshipIds
[[nodiscard]] ExpectedTask< void > validateRecordIds( std::vector< RecordID > record_ids, DbClientPtr db );

constexpr std::chrono::seconds default_max_age {
	std::chrono::duration_cast< std::chrono::seconds >( std::chrono::years( 1 ) )
};

void addFileCacheHeader( drogon::HttpResponsePtr sharedPtr, std::chrono::seconds max_age = default_max_age );

} // namespace idhan::api::helpers
