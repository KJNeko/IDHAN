//
// Created by kj16609 on 8/24/25.
//
#pragma once
#include <json/value.h>

#include <thread>

#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"

namespace idhan::jobs
{

constexpr auto MAX_JOBS_PER_THREAD { 4 };

class JobRuntime
{
	struct WorkerContext
	{
		std::jthread m_thread;
		std::counting_semaphore< MAX_JOBS_PER_THREAD > m_semaphore { MAX_JOBS_PER_THREAD };

		~WorkerContext();
	};

	std::jthread m_manager_thread;
	std::vector< WorkerContext > m_workers {};
	std::size_t m_max_active_jobs;

  public:

	explicit JobRuntime( std::size_t max_active_jobs );

	~JobRuntime();
};

drogon::Task< std::string > createJob( std::string job_type, Json::Value job_data, drogon::orm::DbClientPtr db );

} // namespace idhan::jobs