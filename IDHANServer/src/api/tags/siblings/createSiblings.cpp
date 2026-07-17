//
// Created by Junie on 6/11/26.
//

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

// Sibling resolution was removed in commit 25da681d without removing this endpoint; see
// docs/tag-system-triggers.md §5. Disabled until resolution is re-implemented.
drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagSiblings(
	[[maybe_unused]] const drogon::HttpRequestPtr request )
{
	co_return createNotImplemented( "Tag siblings are not currently resolved; this endpoint is disabled" );
}

} // namespace idhan::api
