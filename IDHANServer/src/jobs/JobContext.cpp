//
// Created by kj16609 on 9/8/25.
//

#include "JobContext.hpp"

#include "drogon/HttpAppFramework.h"
#include "fgl/defines.hpp"

namespace idhan::jobs
{

drogon::Task<> JobContext::markJobPending( drogon::orm::DbClientPtr db )
{
	co_await db->execSqlCoro( "UPDATE jobs SET status = $1 WHERE job_id = $2", "PENDING", m_job_id );
}

drogon::Task<> JobContext::markJobFailed( drogon::orm::DbClientPtr db )
{
	co_await db->execSqlCoro(
		"UPDATE jobs SET status = $1, job_respones = $3 WHERE job_id = $2", "FAILED", m_job_id, m_response );
}

drogon::Task<> JobContext::markJobCompleted( drogon::orm::DbClientPtr db )
{
	co_await db->execSqlCoro(
		"UPDATE jobs SET status = $1, job_response = $3 WHERE job_id = $2", "COMPLETED", m_job_id, m_response );
}

drogon::Task<> JobContext::syncJobStatus( drogon::orm::DbClientPtr db )
{
	switch ( m_job_status )
	{
		default:
			[[fallthrough]];
		case JobStatus::PENDING:
			[[fallthrough]];
		case JobStatus::STARTED:
			{
				co_await markJobPending( db );
				co_return;
			}
		case JobStatus::FAILED:
			{
				co_await markJobFailed( db );
				co_return;
			}
		case JobStatus::COMPLETED:
			{
				co_await markJobCompleted( db );
				co_return;
			}
	}

	FGL_UNREACHABLE();
}

JobContext::~JobContext()
{
	if ( m_job_status != JobStatus::SERIALIZED )
	{
		throw std::runtime_error( "JobContext::~JobContext: JobContext was not serialized" );
	}
}

JobContext::JobContext( const std::string job_id ) : m_job_id( job_id )
{}
} // namespace idhan::jobs