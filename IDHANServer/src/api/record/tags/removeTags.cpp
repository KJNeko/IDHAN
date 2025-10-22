//
// Created by kj16609 on 3/11/25.
//

#include "api/RecordAPI.hpp"
#include "fgl/defines.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::
	removeTags( [[maybe_unused]] const drogon::HttpRequestPtr request, [[maybe_unused]] const RecordID record_id )
{
	FGL_UNIMPLEMENTED();

	auto db { drogon::app().getDbClient() };

	Json::Value ok {};
	ok[ "status" ] = 200;

	co_return drogon::HttpResponse::newHttpJsonResponse( ok );
}

} // namespace idhan::api