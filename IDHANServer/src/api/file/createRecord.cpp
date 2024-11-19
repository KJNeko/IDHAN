//
// Created by kj16609 on 11/17/24.
//

#include "api/IDHANFileAPI.hpp"

namespace idhan::api
{

ResponseTask createRecordFromOctet( const drogon::HttpRequestPtr req )
{
	//TODO: FIXME
}

ResponseTask createRecordFromJson( const drogon::HttpRequestPtr req )
{
	const auto json { req->getBody() };
	// Json::Value root { json };


}

ResponseTask IDHANFileAPI::createRecord( const drogon::HttpRequestPtr request )
{
	// the request here should be either an octet stream, or json. If it's an octet stream, then it will be a file we can hash.

	switch ( request->getContentType() )
	{
		case drogon::CT_APPLICATION_OCTET_STREAM:
			co_return co_await createRecordFromOctet( request );
			break;
		case drogon::CT_APPLICATION_JSON:
			co_return co_await createRecordFromJson( request );
			break;
		default:
			//TODO: Failure
			break;
	}

	co_return nullptr;
}

} // namespace idhan::api
