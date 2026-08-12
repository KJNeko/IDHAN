#include "api/InfoAPI.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > InfoAPI::health( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value json;
	json[ "status" ] = "ok";
	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
