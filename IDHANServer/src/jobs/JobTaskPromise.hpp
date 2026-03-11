//
// Created by kj16609 on 2/27/26.
//
#pragma once
#include <coroutine>
#include <memory>

#include "drogon/HttpResponse.h"

struct JobTaskStatus;
struct JobTask;

struct JobTaskPromise
{
	std::shared_ptr< JobTaskStatus > m_status;

	// Add constructor
	JobTaskPromise();

	// Implement get_return_object
	JobTask get_return_object();

	std::suspend_always initial_suspend();

	std::suspend_always final_suspend() noexcept;

	// Implement return_value overloads
	void return_value( drogon::HttpResponsePtr response );

	void return_value( Json::Value response );

	void unhandled_exception();
};
