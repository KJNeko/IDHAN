//
// Created by kj16609 on 2/27/26.
//

#pragma once

#include "IDHANTypes.hpp"
#include <drogon/HttpResponse.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <source_location>
#include <string>

struct JobTaskStatus
{
	std::optional< drogon::HttpResponsePtr > m_response {};
	std::atomic< bool > m_done { false };
	std::atomic< bool > m_failed { false };
	std::string m_error_message {};
	std::chrono::steady_clock::time_point m_start_time {};
	std::chrono::steady_clock::time_point m_completion_time {};
	bool m_cleanup_requested { false };

	idhan::JobID m_id { 0 };

	std::string m_function_name {};
	std::source_location m_location { std::source_location::current() };

	JobTaskStatus() noexcept = default;
};