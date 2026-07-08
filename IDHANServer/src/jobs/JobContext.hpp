//
// Created by kj16609 on 2/21/26.
//
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <queue>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <coroutine>

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

class JobRuntime
{
	std::unique_ptr< trantor::EventLoopThreadPool > m_pool{};
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

	void enqueue( const std::shared_ptr< JobContext >& ctx );

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

inline SetJobResponseWaitable setJobResponse( drogon::HttpResponsePtr response )
{
	return SetJobResponseWaitable { std::move( response ) };
}

inline SetJobResponseWaitable setJobResponse( const Json::Value& response )
{
	return SetJobResponseWaitable { response };
}

JobRuntime& getJobRuntime();

[[nodiscard]] std::shared_ptr< JobContext > queueJob(
	JobTask task,
	std::string_view name = "",
	std::source_location loc = std::source_location::current() );
