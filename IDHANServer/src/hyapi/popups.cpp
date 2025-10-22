//
// Created by kj16609 on 10/15/25.
//

#include "HyAPI.hpp"

namespace idhan::hyapi
{
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getPopups( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value json {};

	json[ "job_statuses" ] = Json::Value( Json::arrayValue );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::hyapi