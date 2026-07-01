//
// Created by kj16609 on 6/12/25.
//

#include "IDHANTypes.hpp"
#include "api/APIMaintenance.hpp"
#include "jobs/JobContext.hpp"
#include "jobs/JobTask.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"

namespace idhan::api
{

JobTask rescanMetadataJobTask()
{
	auto db { drogon::app().getDbClient() };
	const auto records { co_await db->execSqlCoro( "SELECT record_id FROM file_info" ) };

	std::size_t count { 0 };
	for ( const auto& row : records )
	{
		const auto record_id { row[ "record_id" ].as< RecordID >() };
		co_await metadata::tryParseRecordMetadata( record_id, db );
		++count;
	}

	log::info( "Finished scanning metadata for {} records", count );

	Json::Value result;
	result[ "scanned_count" ] = static_cast< Json::UInt64 >( count );
	co_await setJobResponse( result );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::rescanMetadata(
	[[maybe_unused]] drogon::HttpRequestPtr request )
{
	auto job_ctx { queueJob( rescanMetadataJobTask(), "rescanMetadata" ) };

	Json::Value response;
	response[ "job_id" ] = static_cast< Json::UInt64 >( job_ctx->id() );
	response[ "status" ] = "dispatched";
	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
