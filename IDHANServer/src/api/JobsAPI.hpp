//
// Created by kj16609 on 9/7/25.
//
#pragma once
#include "drogon/HttpController.h"

namespace idhan::api
{

class JobsAPI final : public drogon::HttpController< JobsAPI >
{
	drogon::Task< drogon::HttpResponsePtr > startJob( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > jobStatus( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( JobsAPI::startJob, "/jobs/start" );
	ADD_METHOD_TO( JobsAPI::jobStatus, "/jobs/status" );

	METHOD_LIST_END
};



} // namespace idhan::api