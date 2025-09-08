//
// Created by kj16609 on 8/24/25.
//
#pragma once
#include "JobContext.hpp"
#include "JobRuntime.hpp"

namespace idhan::jobs
{

class ParseJob final : public JobContext
{
  public:

	drogon::Task< void > prepare( drogon::orm::DbClientPtr db ) override;
	drogon::Task< void > run() override;

	Json::Value serialize() override;
	void deserialize( const Json::Value& ) override;
};

} // namespace idhan::jobs