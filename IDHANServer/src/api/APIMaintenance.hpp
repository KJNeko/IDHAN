//
// Created by kj16609 on 3/20/25.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include <drogon/HttpController.h>
#pragma GCC diagnostic pop

#include <expected>

namespace idhan::api
{

class APIMaintenance : public drogon::HttpController< APIMaintenance >
{
	drogon::Task< drogon::HttpResponsePtr > rescanMetadata( drogon::HttpRequestPtr request );
	// drogon::Task< drogon::HttpResponsePtr > postgresqlStorage( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > postgresqlStorageSunData( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > test( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( APIMaintenance::rescanMetadata, "/jobs/metadata/rescan" );
	// ADD_METHOD_TO( IDHANMaintenanceAPI::postgresqlStorage, "/db/stats/chart" );
	ADD_METHOD_TO( APIMaintenance::postgresqlStorageSunData, "/db/stats/sunburst" );

	ADD_METHOD_TO( APIMaintenance::test, "/test" );

	METHOD_LIST_END
};

} // namespace idhan::api