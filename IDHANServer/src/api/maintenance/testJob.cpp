#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "jobs/JobContext.hpp"
#include "jobs/JobTask.hpp"
#include "jobs/JobTaskStatus.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoopThreadPool.h"
#include "trantor/utils/Date.h"

namespace idhan::api
{

// A simple test JobTask that exercises the job system and DB connectivity
JobTask testJobTask()
{
	const auto job_id { co_await getJobID() };
	log::debug( "JOB TEST {}: Entered", job_id );

	auto db { drogon::app().getDbClient() };

	const auto db_result { co_await db->execSqlCoro( "SELECT 1" ) };

	if ( !db_result.empty() )
	{
		const auto num { db_result[ 0 ][ 0 ].as< int >() };
		log::debug( "JOB TEST {}: DB result got: {}", job_id, num );
	}
	else
	{
		log::warn( "JOB TEST {}: DB returned empty result", job_id );
	}

	Json::Value result;
	result[ "message" ] = "Test job completed successfully";
	result[ "job_id" ] = static_cast< Json::UInt64 >( job_id );

	co_await setJobResponse( result );

	log::debug( "JOB TEST {}: Returning", job_id );
	co_return;
}

Json::Value getJobStatusJson(
	idhan::JobID job_id,
	const std::shared_ptr< JobContext >& job,
	bool cleanup_on_completion )
{
	Json::Value response;
	response[ "job_id" ] = static_cast< Json::UInt64 >( job_id );

	if ( !job )
	{
		response[ "status" ] = "not_found";
		return response;
	}

	const auto status = job->status();

	response[ "job_name" ] = status->m_function_name;
	response[ "location" ] = format_ns::format(
		"{}:{}:{}", status->m_location.file_name(), status->m_location.line(), status->m_location.column() );

	const auto now = std::chrono::steady_clock::now();
	const auto system_now = std::chrono::system_clock::now();

	const auto start_time { status->m_start_time.load() };
	if ( start_time != std::chrono::steady_clock::time_point {} )
	{
		const auto start_system = system_now
		                        + std::chrono::duration_cast< std::chrono::system_clock::duration >( start_time - now );
		const auto epoch =
			std::chrono::duration_cast< std::chrono::seconds >( start_system.time_since_epoch() ).count();
		response[ "start_time" ] = static_cast< Json::UInt64 >( epoch );
	}

	if ( job->done() )
	{
		response[ "completed" ] = true;
		if ( status->m_failed )
		{
			response[ "status" ] = "failed";
			response[ "error" ] = status->m_error_message;
		}
		else
		{
			response[ "status" ] = "completed";
		}

		if ( cleanup_on_completion )
		{
			status->m_cleanup_requested = true;
		}
	}
	else
	{
		response[ "completed" ] = false;
		response[ "status" ] = "running";
	}

	if ( status->m_response )
	{
		auto resp = status->m_response.value();
		if ( resp->contentType() == drogon::CT_APPLICATION_JSON )
		{
			response[ "response" ] = *( resp->getJsonObject() );
		}
		else
		{
			response[ "response" ] = std::string( resp->getBody() );
		}
	}

	return response;
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::jobStatus(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	idhan::JobID job_id )
{
	auto& runtime = getJobRuntime();
	auto job = runtime.getJob( job_id );

	if ( !job ) co_return createNotFound( "Job {} not found", job_id );

	co_return drogon::HttpResponse::newHttpJsonResponse( getJobStatusJson( job_id, job, true ) );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::jobsStatus( drogon::HttpRequestPtr request )
{
	auto& runtime = getJobRuntime();
	const bool cleanup = request->getParameter( "cleanup" ) == "true";

	Json::Value response;
	Json::Value jobs_json( Json::arrayValue );

	const auto body { request->getJsonObject() };

	if ( body && body->isObject() && body->isMember( "job_ids" ) && ( *body )[ "job_ids" ].isArray() )
	{
		const auto& job_ids_json = ( *body )[ "job_ids" ];

		for ( const auto& job_id_json : job_ids_json )
		{
			if ( !job_id_json.isIntegral() ) continue;

			const idhan::JobID job_id = job_id_json.asUInt64();
			auto job = runtime.getJob( job_id );

			jobs_json.append( getJobStatusJson( job_id, job, cleanup ) );
		}
	}
	else
	{
		for ( const auto& job : runtime.getAllJobs() )
		{
			jobs_json.append( getJobStatusJson( job->id(), job, cleanup ) );
		}
	}

	response[ "jobs" ] = jobs_json;
	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
