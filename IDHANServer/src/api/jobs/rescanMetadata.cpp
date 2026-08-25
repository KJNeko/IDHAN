#include "IDHANTypes.hpp"
#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "jobs/JobContext.hpp"
#include "jobs/JobTask.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"

namespace idhan::api
{

JobTask rescanMetadataJobTask( std::vector< RecordID > record_ids )
{
	auto db { drogon::app().getDbClient() };

	if ( record_ids.empty() )
	{
		const auto records { co_await db->execSqlCoro( "SELECT record_id FROM file_info" ) };
		record_ids.reserve( records.size() );
		for ( const auto& row : records ) record_ids.emplace_back( row[ "record_id" ].as< RecordID >() );
	}

	std::size_t count { 0 };
	std::size_t failed { 0 };
	for ( const auto record_id : record_ids )
	{
		const auto parsed { co_await metadata::tryParseRecordMetadata( record_id, db ) };
		if ( parsed )
			++count;
		else
			++failed;
	}

	log::info( "Finished scanning metadata for {} records ({} failed)", count, failed );

	Json::Value result;
	result[ "scanned_count" ] = static_cast< Json::UInt64 >( count );
	result[ "failed_count" ] = static_cast< Json::UInt64 >( failed );
	co_await setJobResponse( result );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::rescanMetadata( drogon::HttpRequestPtr request )
{
	// An absent or empty body rescans everything, as it always has.
	std::vector< RecordID > record_ids {};

	if ( const auto json_ptr { request->getJsonObject() }; json_ptr && json_ptr->isMember( "record_ids" ) )
	{
		const auto& ids { ( *json_ptr )[ "record_ids" ] };
		if ( !ids.isArray() ) co_return createBadRequest( "Expected record_ids to be an array of record ids" );

		for ( const auto& id : ids )
		{
			if ( !id.isUInt() ) co_return createBadRequest( "Expected record_ids to contain unsigned integers" );

			record_ids.emplace_back( id.as< RecordID >() );
		}

		if ( record_ids.empty() ) co_return createBadRequest( "Expected at least one record id in record_ids" );

		auto db { drogon::app().getDbClient() };
		const auto validation { co_await helpers::validateRecordIds( record_ids, db ) };
		if ( !validation ) co_return validation.error();
	}

	const auto requested { record_ids.size() };
	auto job_ctx { queueJob( rescanMetadataJobTask( std::move( record_ids ) ), "rescanMetadata" ) };

	Json::Value response;
	response[ "job_id" ] = job_ctx->id();
	response[ "status" ] = "dispatched";
	response[ "record_count" ] = static_cast< Json::UInt64 >( requested );
	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
