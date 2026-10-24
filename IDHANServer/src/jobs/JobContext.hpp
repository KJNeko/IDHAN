#pragma once
#include <folly/executors/CPUThreadPoolExecutor.h>

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
#include "fgl/defines.hpp"

class JobSignaler;
class JobContext;

namespace Json
{
class Value;
}

class JobRuntime
{
	std::unique_ptr< folly::CPUThreadPoolExecutor > m_pool {};
	std::mutex m_queue_mtx {};
	std::condition_variable m_cv;
	std::condition_variable m_cleanup_cv {};
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

	void enqueue( const std::shared_ptr< JobContext >& ctx );

	[[nodiscard]] folly::Executor::KeepAlive<> executor() const;

	[[nodiscard]] std::shared_ptr< JobContext > getJob( idhan::JobID id );

	[[nodiscard]] std::vector< std::shared_ptr< JobContext > > getAllJobs();

	JobRuntime();
	~JobRuntime();

	void requestStop();
};

class JobContext
{
	JobTask m_coro;
	idhan::JobID m_id;

  public:

	JobContext( JobTask&& coro, idhan::JobID id ) noexcept;

	[[nodiscard]] auto id() const { return m_id; }

	[[nodiscard]] bool done() const;

	[[nodiscard]] std::shared_ptr< JobTaskStatus > status() const { return m_coro.m_status; }

	void run();
};

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

[[nodiscard]] inline JobIDWaitable getJobID()
{
	return {};
}

struct SetJobResponseWaitable
{
	Json::Value m_response;

	explicit SetJobResponseWaitable( Json::Value response ) : m_response( std::move( response ) ) {}

	static bool await_ready() noexcept { return false; }

	template < typename Promise >
	bool await_suspend( std::coroutine_handle< Promise > h ) noexcept
	{
		h.promise().m_status->setResponse( std::move( m_response ) );
		return false;
	}

	void await_resume() const noexcept {}
};

inline SetJobResponseWaitable setJobResponse( Json::Value response )
{
	return SetJobResponseWaitable { std::move( response ) };
}

struct SetJobProgressWaitable
{
	std::shared_ptr< const JobProgress > progress;

	static bool await_ready() noexcept { return false; }

	template < typename Promise >
	bool await_suspend( std::coroutine_handle< Promise > handle )
	{
		handle.promise().m_status->setProgress( std::move( progress ) );
		return false;
	}

	void await_resume() const noexcept {}
};

[[nodiscard]] inline SetJobProgressWaitable setJobProgress( std::shared_ptr< const JobProgress > progress )
{
	return SetJobProgressWaitable { std::move( progress ) };
}

JobRuntime& getJobRuntime();

[[nodiscard]] std::shared_ptr< JobContext > queueJob(
	JobTask task,
	std::string_view name = "",
	std::source_location loc = std::source_location::current(),
	bool continuous = false );
