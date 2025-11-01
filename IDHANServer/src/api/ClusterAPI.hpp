//
// Created by kj16609 on 11/15/24.
//
#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "helpers/ExpectedTask.hpp"

namespace idhan::api
{
ExpectedTask< Json::Value > getInfo( ClusterID cluster_id, DbClientPtr transaction );

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
	ADD_METHOD_TO( ClusterAPI::add, "/clusters/add" );
	ADD_METHOD_TO( ClusterAPI::list, "/clusters/list" );
	ADD_METHOD_TO( ClusterAPI::info, "/clusters/{cluster_id}/info" );
	ADD_METHOD_TO( ClusterAPI::modify, "/clusters/{cluster_id}/modify" );
	ADD_METHOD_TO( ClusterAPI::remove, "/clusters/{cluster_id}/remove" );
	ADD_METHOD_TO( ClusterAPI::scan, "/clusters/{cluster_id}/scan" );
	METHOD_LIST_END
};
} // namespace idhan::api
