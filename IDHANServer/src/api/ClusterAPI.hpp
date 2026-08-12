#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include "APIAuth.hpp"
#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "jobs/JobContext.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::api
{
ExpectedTask< Json::Value > getInfo( ClusterID cluster_id, DbClientPtr transaction );

//! Endpoints for managing storage clusters: add, list, info, modify, remove and scan.
class ClusterAPI : public drogon::HttpController< ClusterAPI >
{
	using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;

	ResponseTask modifyT( drogon::HttpRequestPtr request, ClusterID cluster_id, DbClientPtr transaction );

	ResponseTask infoT( drogon::HttpRequestPtr request, ClusterID cluster_id, DbClientPtr transaction );

	ResponseTask add( drogon::HttpRequestPtr request );

	ResponseTask list( drogon::HttpRequestPtr request );

	ResponseTask info( drogon::HttpRequestPtr request, ClusterID cluster_id );

	ResponseTask modify( drogon::HttpRequestPtr request, ClusterID cluster_id );

	ResponseTask remove( drogon::HttpRequestPtr request, ClusterID cluster_id );

	ResponseTask scan( drogon::HttpRequestPtr request, ClusterID cluster_id );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( ClusterAPI::add, "/clusters/add", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( ClusterAPI::list, "/clusters/list", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( ClusterAPI::info, "/clusters/{cluster_id}/info", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( ClusterAPI::modify, "/clusters/{cluster_id}/modify", drogon::Patch, IDHANAPIAuthName );
	ADD_METHOD_TO( ClusterAPI::remove, "/clusters/{cluster_id}/remove", drogon::Delete, IDHANAPIAuthName );
	ADD_METHOD_TO( ClusterAPI::scan, "/clusters/{cluster_id}/scan", drogon::Post, IDHANAPIAuthName );
	METHOD_LIST_END
};
} // namespace idhan::api
