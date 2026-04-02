//
// Created by kj16609 on 2/27/26.
//

#include <chrono>
#include <thread>

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

// A simple test JobTask that simulates some asynchronous work
JobTask testJobTask()
{
	const auto job_id { co_await getJobID() };
	log::debug( "JOB TEST {}: Entered", job_id );
	std::this_thread::sleep_for( std::chrono::seconds( 10 ) );
	log::debug( "JOB TEST {}: Slept", job_id );

	auto db { drogon::app().getDbClient() };

	const auto db_result { co_await db->execSqlCoro( "SELECT 1" ) };

	if ( !db_result.empty() )
	{
		const auto num { db_result[ 0 ][ 0 ].as< int >() };

		log::debug( "Result got: {}", num );
	}
	else
	{
		log::warn( "Whut" );
	}

	// Return a success response
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

	if ( status->m_start_time != std::chrono::steady_clock::time_point {} )
	{
		const auto start_system =
			system_now
			+ std::chrono::duration_cast< std::chrono::system_clock::duration >( status->m_start_time - now );
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
		}

		if ( cleanup_on_completion )
		{
			// Requirement: delete it after we've checked for it once.
			status->m_cleanup_requested = true;
		}
	}
	else
	{
		response[ "completed" ] = false;
		response[ "status" ] = "running";
	}

	return response;
}

// Endpoint handler: Dispatches the test job and returns its ID
drogon::Task< drogon::HttpResponsePtr > APIMaintenance::testJob( drogon::HttpRequestPtr request )
{
	// Dispatch the test job to the job system
	auto job_ctx = queueJob( testJobTask(), "testJobTask" );
	log::debug( "Job created" );

	// Prepare response with job details
	Json::Value response;
	response[ "job_id" ] = static_cast< Json::Int64 >( job_ctx->id() );
	response[ "status" ] = "dispatched";
	response[ "message" ] = "Test job has been dispatched. Check /jobs/{job_id}/status for updates.";

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::jobStatus( drogon::HttpRequestPtr request, idhan::JobID job_id )
{
	auto& runtime = getJobRuntime();
	auto job = runtime.getJob( job_id );

	if ( !job )
	{
		co_return drogon::HttpResponse::newNotFoundResponse();
	}

	Json::Value response = getJobStatusJson( job_id, job, true );

	// Requirement: If the job is not complete. Return the full json body as well as the response
	// (Assuming "full json body" refers to the request body if it was JSON)
	if ( !job->done() )
	{
		if ( request->contentType() == drogon::CT_APPLICATION_JSON )
		{
			if ( auto req_body = request->getJsonObject() )
			{
				response[ "request_body" ] = *req_body;
			}
		}
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::jobsStatus( drogon::HttpRequestPtr request )
{
	auto& runtime = getJobRuntime();
	const bool cleanup = request->getParameter( "cleanup" ) == "true";

	Json::Value response;
	Json::Value jobs_json( Json::arrayValue );

	const auto body { request->getJsonObject() };

	// If a list of job IDs is provided, return their status
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
		// Requirement: Return all currently in progress jobs
		for ( const auto& job : runtime.getAllJobs() )
		{
			jobs_json.append( getJobStatusJson( job->id(), job, cleanup ) );
		}
	}

	response[ "jobs" ] = jobs_json;
	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api