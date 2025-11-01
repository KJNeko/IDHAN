//
// Created by kj16609 on 11/18/24.
//

#include "api/ClusterAPI.hpp"
#include "fixme.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::
	remove( [[maybe_unused]] drogon::HttpRequestPtr request, [[maybe_unused]] const ClusterID cluster_id )
{
	// TODO: Implement removal logic
	idhan::fixme();

	co_return drogon::HttpResponse::newHttpJsonResponse( Json::Value() );
}

} // namespace idhan::api
