//
// Created by kj16609 on 3/20/25.
//
#pragma once

#include <drogon/HttpController.h>

#include <expected>

namespace idhan::api
{

class IDHANMaintenanceAPI : public drogon::HttpController< IDHANMaintenanceAPI >
{
	drogon::Task< drogon::HttpResponsePtr > rescanMetadata( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANMaintenanceAPI::rescanMetadata, "/jobs/metadata/rescan" );

	METHOD_LIST_END
};

} // namespace idhan::api