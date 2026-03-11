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

namespace idhan::api
{

// A simple test JobTask that simulates some asynchronous work
JobTask testJobTask()
{
	log::debug( "JOB TEST: Entered" );
	// Optionally simulate a delay (in a real scenario, this could be async I/O)
	// Note: In production, use proper async mechanisms; this is for testing
	std::this_thread::sleep_for( std::chrono::seconds( 60 ) );
	log::debug( "JOB TEST: Slept" );

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
	log::debug( "JOB TEST: Returning" );
	co_return drogon::HttpResponse::newHttpJsonResponse( result );
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

	Json::Value response;
	if ( !job )
	{
		response[ "status" ] = "not_found";
		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}

	const auto status = job->status();
	const bool cleanup = request->getParameter( "cleanup" ) == "true";

	response[ "job_name" ] = status->m_function_name;
	response[ "location" ] = format_ns::format(
		"{}:{}:{}", status->m_location.file_name(), status->m_location.line(), status->m_location.column() );

	if ( job->done() )
	{
		if ( status->m_failed )
		{
			response[ "status" ] = "failed";
			response[ "error" ] = status->m_error_message;
			if ( cleanup )
			{
				status->m_cleanup_requested = true;
				response[ "message" ] = "Job status marked for cleanup.";
			}
		}
		else
		{
			response[ "status" ] = "completed";
			if ( status->m_response )
			{
				// If the job returned an actual HttpResponse, we might want to return its body or the whole thing.
				// But the prompt says "Show the job completion status".
				// For now let's just indicate it's done.
				response[ "result" ] = "See individual job result if applicable";
			}
		}
	}
	else
	{
		response[ "status" ] = "running";
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::jobsStatus( drogon::HttpRequestPtr request )
{
	const auto body { request->getJsonObject() };
	if ( !body || !body->isObject() || !body->isMember( "job_ids" ) || !( *body )[ "job_ids" ].isArray() )
	{
		co_return createBadRequest( "Missing or invalid job_ids array in request body" );
	}

	const auto& job_ids_json = ( *body )[ "job_ids" ];
	auto& runtime = getJobRuntime();
	const bool cleanup = request->getParameter( "cleanup" ) == "true";

	Json::Value response;
	Json::Value jobs_json( Json::arrayValue );

	for ( const auto& job_id_json : job_ids_json )
	{
		if ( !job_id_json.isIntegral() ) continue;

		const idhan::JobID job_id = job_id_json.asUInt64();
		auto job = runtime.getJob( job_id );

		Json::Value job_status_json;
		job_status_json[ "job_id" ] = static_cast< Json::UInt64 >( job_id );

		if ( !job )
		{
			job_status_json[ "status" ] = "not_found";
		}
		else
		{
			const auto status = job->status();
			job_status_json[ "job_name" ] = status->m_function_name;
			job_status_json[ "location" ] = format_ns::format(
				"{}:{}:{}", status->m_location.file_name(), status->m_location.line(), status->m_location.column() );

			if ( job->done() )
			{
				if ( status->m_failed )
				{
					job_status_json[ "status" ] = "failed";
					job_status_json[ "error" ] = status->m_error_message;
					if ( cleanup )
					{
						status->m_cleanup_requested = true;
					}
				}
				else
				{
					job_status_json[ "status" ] = "completed";
				}
			}
			else
			{
				job_status_json[ "status" ] = "running";
			}
		}
		jobs_json.append( job_status_json );
	}

	response[ "jobs" ] = jobs_json;
	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api