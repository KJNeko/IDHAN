#include "JobContext.hpp"

#include <folly/executors/thread_factory/NamedThreadFactory.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

#include "Config.hpp"
#include "JobTaskStatus.hpp"
#include "logging/log.hpp"

inline static std::atomic< idhan::JobID > job_id_counter { 1 };

constexpr auto job_retention_period { std::chrono::hours( 1 ) };

//! How often cleanup() sweeps finished jobs. Also the practical upper bound on how long a completed
//! job's result remains fetchable while job_retention_period is zero.
constexpr auto job_cleanup_interval { std::chrono::seconds( 10 ) };

using namespace idhan;

std::shared_ptr< JobContext > JobRuntime::getNextJob()
{
	std::unique_lock lock { m_queue_mtx };
	m_cv.wait(
		lock,
		[ this ]()
		{
			return !m_queue.empty() || m_hard_stop.load( std::memory_order_acquire )
		        || m_soft_stop.load( std::memory_order_acquire );
		} );

	std::shared_ptr< JobContext > job { nullptr };

	if ( m_queue.empty() ) return job;

	job = m_queue.front();
	m_queue.pop();
	return job;
}

void JobRuntime::runner()
{
	while ( !m_hard_stop.load( std::memory_order_acquire ) )
	{
		if ( m_soft_stop.load( std::memory_order_acquire ) )
		{
			// the queue must not be inspected without the mutex; enqueue() mutates it concurrently
			std::lock_guard lock { m_queue_mtx };
			if ( m_queue.empty() ) break;
		}

		const auto job { getNextJob() };
		if ( !job ) continue;

		log::trace( "Acquired job {}", job->id() );

		m_pool->add( [ job ]() { job->run(); } );

		log::trace( "Dispatched job {}", job->id() );
	}
}

void JobRuntime::cleanup()
{
	std::unique_lock lock { m_queue_mtx };
	while ( !m_hard_stop.load( std::memory_order_acquire ) )
	{
		std::erase_if(
			m_jobs,
			[ this ]( const auto& item )
			{
				const auto& job = item.second;
				if ( !job->done() ) return false;

				const auto status = job->status();
				if ( !status ) return true;

				const auto now = std::chrono::steady_clock::now();
				const bool remove {
					status->m_cleanup_requested || now - status->m_completion_time >= job_retention_period
				};
				if ( remove ) m_events->remove( item.first );
				return remove;
			} );
		m_cleanup_cv.wait_for(
			lock, job_cleanup_interval, [ this ] { return m_hard_stop.load( std::memory_order_acquire ); } );
	}
}

void JobRuntime::enqueue( const std::shared_ptr< JobContext >& ctx )
{
	m_events->add( ctx->status() );
	{
		std::lock_guard< std::mutex > lock { m_queue_mtx };
		m_queue.push( ctx );
		m_jobs[ ctx->id() ] = ctx;
	}
	m_cv.notify_one();
}

folly::Executor::KeepAlive<> JobRuntime::executor() const
{
	return folly::getKeepAliveToken( m_pool.get() );
}

std::shared_ptr< JobContext > JobRuntime::getJob( idhan::JobID id )
{
	std::lock_guard lock { m_queue_mtx };
	if ( m_jobs.contains( id ) ) return m_jobs.at( id );
	return nullptr;
}

std::vector< std::shared_ptr< JobContext > > JobRuntime::getAllJobs()
{
	std::lock_guard lock { m_queue_mtx };
	std::vector< std::shared_ptr< JobContext > > result;
	result.reserve( m_jobs.size() );
	for ( const auto& job : m_jobs | std::views::values )
	{
		result.push_back( job );
	}
	return result;
}

JobRuntime::JobRuntime() : m_pool( nullptr ), m_queue_mtx(), m_cv(), m_queue(), m_runner_thread(), m_cleanup_thread()
{
	const auto hardware { std::max( std::thread::hardware_concurrency(), 1u ) };

	// 25% of the hardware threads are for jobs, with a min of 2
	auto job_thread_count { idhan::config::getSilentDefault< std::size_t >(
		"server", "job_threads", std::max< std::size_t >( hardware / 4, 2 ) ) };

	// Zero asks for the whole machine, as it does everywhere else. Taken literally it would build a
	// pool with no loops in it, and getNextLoop() hands back nullptr.
	if ( job_thread_count == 0 ) job_thread_count = hardware;

	job_thread_count = std::max< std::size_t >( job_thread_count, 2 );

	m_pool = std::make_unique< folly::CPUThreadPoolExecutor >(
		job_thread_count, std::make_shared< folly::NamedThreadFactory >( "idhan-job" ) );

	m_runner_thread = std::jthread( [ this ]() { runner(); } );
	m_cleanup_thread = std::jthread( [ this ]() { cleanup(); } );
}

using Clock = std::chrono::steady_clock;

void JobRuntime::requestStop()
{
	m_soft_stop.store( true, std::memory_order_release );
	m_cv.notify_all();
}

JobRuntime::~JobRuntime()
{
	requestStop();

	const auto begin_stop { Clock::now() };
	const auto timeout { std::chrono::seconds( 10 ) };

	while ( Clock::now() < begin_stop + timeout )
	{
		bool all_finished { false };
		{
			std::lock_guard lock { m_queue_mtx };
			all_finished =
				m_queue.empty()
				&& std::ranges::all_of( m_jobs | std::views::values, []( const auto& job ) { return job->done(); } );
		}
		if ( all_finished ) break;
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	}

	m_hard_stop.store( true, std::memory_order_release );
	m_cv.notify_all();
	m_cleanup_cv.notify_all();
}

JobContext::JobContext( JobTask&& coro, const idhan::JobID id ) noexcept : m_coro( std::move( coro ) ), m_id( id )
{}

bool JobContext::done() const
{
	return m_coro.m_status && m_coro.m_status->m_done;
}

void JobContext::run()
{
	if ( m_coro.m_handle.done() ) return;

	if ( m_coro.m_status && m_coro.m_status->m_start_time.load() == std::chrono::steady_clock::time_point {} )
	{
		m_coro.m_status->m_start_time = std::chrono::steady_clock::now();
		m_coro.m_status->notifyChanged();
	}

	log::trace( "Resuming job {}", m_id );
	m_coro.m_handle.resume();
}

idhan::JobID generateNewJobID()
{
	return job_id_counter++;
}

JobRuntime& getJobRuntime()
{
	static JobRuntime job_runtime;
	return job_runtime;
}

std::shared_ptr< JobContext > queueJob(
	JobTask task,
	const std::string_view name,
	const std::source_location loc,
	const bool continuous )
{
	const auto job_id { generateNewJobID() };

	if ( task.m_status )
	{
		task.m_status->m_id = job_id;
		task.m_status->m_continuous = continuous;
		task.m_status->m_function_name = name;
		task.m_status->m_location = loc;
	}

	auto ctx { std::make_shared< JobContext >( std::move( task ), job_id ) };

	getJobRuntime().enqueue( ctx );
	log::debug( "Queued job {} ({})", job_id, name );

	return ctx;
}
