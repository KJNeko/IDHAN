//
// Created by kj16609 on 3/11/25.
//

#include "api/RecordAPI.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::
	removeTags( const drogon::HttpRequestPtr request, const RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	Json::Value ok {};
	ok[ "status" ] = 200;

	co_return drogon::HttpResponse::newHttpJsonResponse( ok );
}

} // namespace idhan::api