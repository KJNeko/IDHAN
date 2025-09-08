//
// Created by kj16609 on 8/24/25.
//
#include "JobRuntime.hpp"

#include "drogon/HttpAppFramework.h"

namespace idhan::jobs
{

JobRuntime::WorkerContext::~WorkerContext()
{
	m_thread.request_stop();
	m_thread.join();
}

void jobManager( std::stop_token token )
{
	auto db { drogon::app().getDbClient() };
	while ( !token.stop_requested() )
	{
		const auto job_response {
			db->execSqlSync( "SELECT job_id, job_data FROM jobs WHERE job_status = 'PENDING' LIMIT 1" )
		};

		if ( job_response.empty() )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
			continue;
		}
	}
}

JobRuntime::JobRuntime( const std::size_t max_active_jobs ) :
  m_manager_thread( jobManager ),
  m_max_active_jobs( max_active_jobs )
{}

JobRuntime::~JobRuntime()
{}
} // namespace idhan::jobs