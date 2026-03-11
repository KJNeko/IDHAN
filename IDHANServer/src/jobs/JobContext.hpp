//
// Created by kj16609 on 2/21/26.
//
#pragma once
#include <condition_variable>
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
	std::unique_ptr< trantor::EventLoopThreadPool > m_pool;
	std::mutex m_queue_mtx {};
	std::condition_variable m_cv;
	using JobPtr = std::shared_ptr< JobContext >;
	std::queue< std::shared_ptr< JobContext > > m_queue {};
	std::unordered_map< idhan::JobID, JobPtr > m_jobs {};

	bool m_soft_stop { false };
	bool m_hard_stop { false };

	std::jthread m_runner_thread;
	std::jthread m_cleanup_thread;

	std::shared_ptr< JobContext > getNextJob();

	void runner();

	void cleanup();

  public:

	void enqueue( const std::shared_ptr< JobContext >& ctx );

	std::shared_ptr< JobContext > getJob( idhan::JobID id );

	JobRuntime();
	~JobRuntime();
};

class JobContext
{
	JobTask m_coro;
	idhan::JobID m_id;

  public:

	JobContext( JobTask&& coro, idhan::JobID id ) noexcept;

	auto id() const { return m_id; }

	bool done() const;

	std::shared_ptr< JobTaskStatus > status() const { return m_coro.m_status; }

	bool run();
};

idhan::JobID getJobID();

JobRuntime& getJobRuntime();

std::shared_ptr< JobContext >
  queueJob( JobTask task, std::string_view name = "", std::source_location loc = std::source_location::current() );
