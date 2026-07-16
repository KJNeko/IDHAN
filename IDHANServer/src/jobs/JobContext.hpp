//
// Created by kj16609 on 2/21/26.
//
#pragma once
#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <memory>
#include <queue>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "IDHANTypes.hpp"
#include "JobTask.hpp"
#include "JobTaskStatus.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"
#include "trantor/net/EventLoopThreadPool.h"

class JobSignaler;
class JobContext;

namespace Json
{
class Value;
};

//! Runs JobTask coroutines on a dedicated trantor event-loop thread pool. Jobs are process-local
//! (IDs are not persisted and reset on restart) and completed jobs are retained for one hour. Access
//! the single instance via getJobRuntime(); enqueue work with queueJob().
class JobRuntime
{
	std::unique_ptr< trantor::EventLoopThreadPool > m_pool {};
	std::mutex m_queue_mtx {};
	std::condition_variable m_cv;
	using JobPtr = std::shared_ptr< JobContext >;
	std::queue< std::shared_ptr< JobContext > > m_queue {};
	std::unordered_map< idhan::JobID, JobPtr > m_jobs {};

	std::atomic< bool > m_soft_stop { false };
	std::atomic< bool > m_hard_stop { false };

	std::jthread m_runner_thread;
	std::jthread m_cleanup_thread;

	std::shared_ptr< JobContext > getNextJob();

	void runner();

	void cleanup();

  public:

	//! Adds a job to the run queue and wakes the runner thread. Prefer queueJob() at call sites.
	void enqueue( const std::shared_ptr< JobContext >& ctx );

	//! \return The tracked job with \p id, or nullptr if it is unknown or has been cleaned up.
	[[nodiscard]] std::shared_ptr< JobContext > getJob( idhan::JobID id );

	//! \return All currently tracked jobs (queued, running and recently completed).
	[[nodiscard]] std::vector< std::shared_ptr< JobContext > > getAllJobs();

	JobRuntime();
	~JobRuntime();

	//! Requests shutdown; in-flight jobs are allowed to finish before the runner stops.
	void requestStop();
};

//! Owns a single job's running coroutine and its process-local JobID, and exposes the job's status
//! to the /jobs status endpoints.
class JobContext
{
	JobTask m_coro;
	idhan::JobID m_id;

  public:

	JobContext( JobTask&& coro, idhan::JobID id ) noexcept;

	//! \return This job's process-local ID.
	[[nodiscard]] auto id() const { return m_id; }

	//! \return true once the job's coroutine has run to completion.
	[[nodiscard]] bool done() const;

	//! \return The shared status object holding the job's result response and completion flag.
	[[nodiscard]] std::shared_ptr< JobTaskStatus > status() const { return m_coro.m_status; }

	//! Runs/resumes the job's coroutine.
	void run();
};

//! \return A fresh, process-local job ID.
[[nodiscard]] idhan::JobID generateNewJobID();

struct JobIDWaitable
{
	idhan::JobID m_id { 0 };

	static bool await_ready() noexcept { return false; }

	template < typename Promise >
	bool await_suspend( std::coroutine_handle< Promise > h ) noexcept
	{
		static_assert( requires { h.promise().m_status->m_id; }, "Promise must have m_status->m_id" );
		m_id = h.promise().m_status->m_id;
		return false;
	}

	[[nodiscard]] idhan::JobID await_resume() const noexcept { return m_id; }
};

//! From inside a job coroutine, `co_await getJobID()` yields that job's JobID.
[[nodiscard]] inline JobIDWaitable getJobID()
{
	return {};
}

struct SetJobResponseWaitable
{
	drogon::HttpResponsePtr m_response;

	explicit SetJobResponseWaitable( drogon::HttpResponsePtr response ) : m_response( std::move( response ) ) {}

	explicit SetJobResponseWaitable( const Json::Value& response ) :
	  m_response( drogon::HttpResponse::newHttpJsonResponse( response ) )
	{}

	static bool await_ready() noexcept { return false; }

	template < typename Promise >
	bool await_suspend( std::coroutine_handle< Promise > h ) noexcept
	{
		static_assert( requires { h.promise().m_status->m_response; }, "Promise must have m_status->m_response" );
		h.promise().m_status->m_response = m_response;
		return false;
	}

	void await_resume() const noexcept {}
};

//! From inside a job coroutine, `co_await setJobResponse(response_or_json)` stores the result that
//! the /jobs status endpoints will later return for this job.
inline SetJobResponseWaitable setJobResponse( drogon::HttpResponsePtr response )
{
	return SetJobResponseWaitable { std::move( response ) };
}

//! \copydoc setJobResponse(drogon::HttpResponsePtr)
inline SetJobResponseWaitable setJobResponse( const Json::Value& response )
{
	return SetJobResponseWaitable { response };
}

//! \return The process-wide JobRuntime singleton.
JobRuntime& getJobRuntime();

//! Enqueues \p task as a new job and returns its JobContext immediately (endpoints respond with the
//! job_id and clients poll for status).
//! \param name Human-readable label used in logging.
//! \param loc Captured call site, for diagnostics.
[[nodiscard]] std::shared_ptr< JobContext > queueJob(
	JobTask task,
	std::string_view name = "",
	std::source_location loc = std::source_location::current() );
