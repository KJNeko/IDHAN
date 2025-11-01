//
// Created by kj16609 on 6/12/25.
//

#include "IDHANTypes.hpp"
#include "api/APIMaintenance.hpp"
#include "metadata/parseMetadata.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::rescanMetadata( [[maybe_unused]] drogon::HttpRequestPtr
                                                                            request )
{
	auto db { drogon::app().getDbClient() };
	const auto records { co_await db->execSqlCoro( "SELECT record_id FROM file_info" ) };

	for ( const auto& row : records )
	{
		const auto record_id { row[ "record_id" ].as< RecordID >() };

		co_await tryParseRecordMetadata( record_id, db );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( Json::Value() );
}

} // namespace idhan::api
