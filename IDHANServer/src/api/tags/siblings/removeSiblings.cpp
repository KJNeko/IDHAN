#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

// See createSiblings.cpp.
drogon::Task< drogon::HttpResponsePtr > TagAPI::removeTagSiblings( [[maybe_unused]] const drogon::HttpRequestPtr request )
{
	co_return createNotImplemented( "Tag siblings are not currently resolved; this endpoint is disabled" );
}

} // namespace idhan::api
