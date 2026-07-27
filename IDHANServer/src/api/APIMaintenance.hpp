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

#include "APIAuth.hpp"
#include "IDHANTypes.hpp"
#include "hyapi/HyAPIAuth.hpp"

namespace idhan::api
{

//! Maintenance and administrative endpoints: metadata rescans, DB storage stats, MIME
//! parse/thumbnail/reload/parser-listing, integrity checks, thumbnail purge, and job-status polling.
class APIMaintenance : public drogon::HttpController< APIMaintenance >
{
	drogon::Task< drogon::HttpResponsePtr > rescanMetadata( drogon::HttpRequestPtr request );
	// drogon::Task< drogon::HttpResponsePtr > postgresqlStorage( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > postgresqlStorageSunData( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > databaseStats( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > test( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > integrityCheck( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > parseMime( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createThumbnail( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > reloadMime( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > listParsers( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > purgeThumbnails( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > testJob( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > jobStatus( drogon::HttpRequestPtr request, idhan::JobID job_id );
	drogon::Task< drogon::HttpResponsePtr > jobsStatus( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( APIMaintenance::rescanMetadata, "/jobs/metadata/rescan", drogon::Post, IDHANAPIAuthName );
	// ADD_METHOD_TO( IDHANMaintenanceAPI::postgresqlStorage, "/db/stats/chart", IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::postgresqlStorageSunData, "/db/stats/sunburst", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::databaseStats, "/db/stats", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( APIMaintenance::parseMime, "/mime/parse", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::createThumbnail, "/mime/generate_thumbnail", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::reloadMime, "/mime/reload", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::listParsers, "/mime/parsers", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( APIMaintenance::integrityCheck, "/integrity", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( APIMaintenance::purgeThumbnails, "/purge/thumbnails", drogon::Post, IDHANAPIAuthName );

	ADD_METHOD_TO( APIMaintenance::testJob, "/test", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::jobStatus, "/jobs/{job_id}/status", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( APIMaintenance::jobsStatus, "/jobs/status", drogon::Get, IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
