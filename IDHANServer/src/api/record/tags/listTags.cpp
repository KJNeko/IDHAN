//
// Created by kj16609 on 3/11/25.
//

#include "api/IDHANRecordAPI.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::
	listTags( const drogon::HttpRequestPtr request, const RecordID record_id )
{}

} // namespace idhan::api