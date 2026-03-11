//
// Created by kj16609 on 2/21/26.
//
#include "JobContext.hpp"

#include <chrono>
#include <mutex>
#include <thread>

#include "Config.hpp"
#include "JobTaskStatus.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoopThreadPool.h"

inline static std::atomic< idhan::JobID > job_id_counter {};

using namespace idhan;

std::shared_ptr< JobContext > JobRuntime::getNextJob()
{
	std::unique_lock lock { m_queue_mtx };
	m_cv.wait( lock, [ this ]() { return !m_queue.empty() || m_hard_stop || m_soft_stop; } );

	if ( m_queue.empty() ) return nullptr;

	auto job { m_queue.front() };
	m_queue.pop();
	return job;
}

const auto active_jobs_per_thread { 4 };

void JobRuntime::runner()
{
	while ( !m_hard_stop )
	{
		if ( m_soft_stop && m_queue.empty() ) break;

		const auto job { getNextJob() };
		if ( !job ) continue;

		log::debug( "Acquired Job" );

		// get loop
		trantor::EventLoop* loop { nullptr };
		do
		{
			loop = m_pool->getNextLoop();
		}
		while ( !loop );

		loop->queueInLoop( [ job ]() { job->run(); } );

		log::debug( "Job dispatched to thread" );
	}
}

void JobRuntime::cleanup()
{
	while ( !m_hard_stop )
	{
		{
			std::lock_guard lock { m_queue_mtx };
			std::erase_if(
				m_jobs,
				[]( const auto& item )
				{
					const auto& job = item.second;
					if ( !job->done() ) return false;

					const auto status = job->status();
					if ( !status ) return true; // Should not happen

					if ( status->m_failed )
					{
						return status->m_cleanup_requested;
					}

					// Successful job
					const auto now = std::chrono::steady_clock::now();
					const auto retention_period = std::chrono::minutes( 10 );
					return ( now - status->m_completion_time ) > retention_period;
				} );
		}
		std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
	}
}

void JobRuntime::enqueue( const std::shared_ptr< JobContext >& ctx )
{
	{
		std::lock_guard< std::mutex > lock { m_queue_mtx };
		m_queue.push( ctx );
		m_jobs[ ctx->id() ] = ctx;
	}
	m_cv.notify_one();
}

std::shared_ptr< JobContext > JobRuntime::getJob( idhan::JobID id )
{
	std::lock_guard lock { m_queue_mtx };
	if ( m_jobs.contains( id ) ) return m_jobs.at( id );
	return nullptr;
}

JobRuntime::JobRuntime() : m_pool( nullptr ), m_queue_mtx(), m_cv(), m_queue(), m_runner_thread(), m_cleanup_thread()
{
	// 25% of the hardware threads are for jobs, with a min of 2
	const auto job_thread_count { idhan::config::getSilentDefault< std::size_t >(
		"server", "job_threads", std::max( std::thread::hardware_concurrency() / 4ul, 2ul ) ) };

	m_pool = std::make_unique< trantor::EventLoopThreadPool >( job_thread_count );
	m_pool->start();

	m_runner_thread = std::jthread( [ this ]() { runner(); } );
	m_cleanup_thread = std::jthread( [ this ]() { cleanup(); } );
}

using Clock = std::chrono::steady_clock;

JobRuntime::~JobRuntime()
{
	m_soft_stop = true;
	m_cv.notify_all();

	const auto begin_stop { Clock::now() };
	const auto timeout { std::chrono::seconds( 10 ) };

	while ( Clock::now() < begin_stop + timeout )
	{
		{
			std::lock_guard lock { m_queue_mtx };
			if ( m_queue.empty() ) break;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	}

	m_hard_stop = true;
	m_cv.notify_all();
}

JobContext::JobContext( JobTask&& coro, const idhan::JobID id ) noexcept : m_coro( std::move( coro ) ), m_id( id )
{}

bool JobContext::done() const
{
	return m_coro.m_status && m_coro.m_status->m_done;
}

bool JobContext::run()
{
	if ( m_coro.m_handle.done() ) return true;

	m_coro.m_handle.resume();

	return m_coro.m_handle.done();
}

idhan::JobID getJobID()
{
	return job_id_counter++;
}

static std::unique_ptr< JobRuntime > job_runtime { nullptr };

JobRuntime& getJobRuntime()
{
	if ( !job_runtime ) job_runtime = std::make_unique< JobRuntime >();
	return *job_runtime;
}

std::shared_ptr< JobContext > queueJob( JobTask task, const std::string_view name, const std::source_location loc )
{
	const auto job_id { getJobID() };

	if ( task.m_status )
	{
		task.m_status->m_function_name = name;
		task.m_status->m_location = loc;
	}

	auto ctx { std::make_shared< JobContext >( std::move( task ), job_id ) };

	getJobRuntime().enqueue( ctx );
	log::debug( "Queued Job" );

	return ctx;
}