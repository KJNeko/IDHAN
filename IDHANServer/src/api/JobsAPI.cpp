//
// Created by kj16609 on 9/7/25.
//

#include "JobsAPI.hpp"

#include <trantor/utils/Date.h>

#include "IDHANTypes.hpp"
#include "helpers/createBadRequest.hpp"
#include "jobs/JobContext.hpp"

namespace idhan::api
{

using JobInitFunc = std::function< drogon::Task< drogon::HttpResponsePtr >( drogon::HttpRequestPtr ) >;

drogon::Task< std::string > createJob( std::string job_type, const Json::Value job_data, drogon::orm::DbClientPtr db )
{
	const auto job_response { co_await db->execSqlCoro(
		"INSERT INTO jobs (job_id, job_type, job_data) VALUES (gen_random_uuid(), $1, $2) RETURNING job_id",
		job_type,
		job_data ) };

	const auto job_id { job_response[ 0 ][ "job_id" ].as< std::string >() };

	co_return job_id;
}

void applyJsonFlag( Json::Value& json, const std::string& name, const bool default_val, drogon::HttpRequestPtr request )
{
	if ( const auto opt = request->getOptionalParameter< bool >( name ); opt )
		json[ name ] = *opt;
	else
		json[ name ] = default_val;
}

drogon::Task< drogon::HttpResponsePtr > startJobClusterScan( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	const auto opt_cluster_id { request->getOptionalParameter< ClusterID >( "cluster_id" ) };

	if ( !opt_cluster_id.has_value() ) co_return createBadRequest( "Must provide cluster_id for job cluster_scan" );

	Json::Value job_json {};
	job_json[ "cluster_id" ] = *opt_cluster_id;
	job_json[ "version" ] = 1;

	// Adopts any orphan files found into IDHAN
	applyJsonFlag( job_json, "adopt_orphans", false, request );

	// Force the hash to be recomputed
	applyJsonFlag( job_json, "recompute_hash", true, request );

	// Scan mime for files that do not have it
	applyJsonFlag( job_json, "scan_mime", true, request );

	// Rescan mime (overwrite all previous mime info for files)
	applyJsonFlag( job_json, "rescan_mime", false, request );

	// Scans for metadata (image resolution, ect)
	applyJsonFlag( job_json, "scan_metadata", true, request );

	// Rescans metadata (overwrite all previous metadata for files)
	applyJsonFlag( job_json, "rescan_metadata", false, request );

	Json::Value response_json {};
	response_json[ "job_id" ] = co_await createJob( "cluster_scan", job_json, db );
	response_json[ "data" ] = job_json;

	co_return drogon::HttpResponse::newHttpJsonResponse( response_json );
}

inline static std::unordered_map< std::string, JobInitFunc > job_init_funcs { { "cluster_scan", startJobClusterScan } };

drogon::Task< drogon::HttpResponsePtr > JobsAPI::startJob( const drogon::HttpRequestPtr request )
{
	const auto job_type { request->getOptionalParameter< std::string >( "type" ) };

	if ( !job_type ) co_return createBadRequest( "Must provide job type" );

	auto itter { job_init_funcs.find( *job_type ) };

	if ( itter == job_init_funcs.end() )
		co_return createBadRequest( "Invalid job type. No internal function for handling job \'{}\'", *job_type );

	auto db { drogon::app().getDbClient() };

	co_return co_await itter->second( request );
}

drogon::Task< Json::Value > getJobJson( const std::string job_id, drogon::orm::DbClientPtr db )
{
	const auto job_result { co_await db->execSqlCoro( "SELECT * FROM jobs WHERE job_id = $1", job_id ) };

	Json::Value json {};

	if ( job_result.empty() )
	{
		json[ "job_id" ] = job_id;
		json[ "status" ] = "not found";
		co_return json;
	}

	json[ "job_id" ] = job_id;
	const auto job_json { job_result[ 0 ][ "job_data" ].as< Json::Value >() };
	json[ "data" ] = job_json;

	const auto job { job_result[ 0 ] };

	if ( !job[ "time_requested" ].isNull() )
	{
		const auto time_requested { trantor::Date::fromDbString( job[ "time_requested" ].as< std::string >() ) };

		json[ "time_requested" ][ "human" ] = job[ "time_requested" ].as< std::string >();
		json[ "time_requested" ][ "unix" ] = time_requested.secondsSinceEpoch();
	}
	else
	{
		json[ "time_requested" ] = Json::Value( Json::nullValue );
	}

	if ( !job[ "time_completed" ].isNull() )
	{
		const auto time_requested { trantor::Date::fromDbString( job[ "time_completed" ].as< std::string >() ) };

		json[ "time_completed" ][ "human" ] = job[ "time_completed" ].as< std::string >();
		json[ "time_completed" ][ "unix" ] = time_requested.secondsSinceEpoch();
	}
	else
	{
		json[ "time_completed" ] = Json::Value( Json::nullValue );
	}

	co_return json;
}

Json::Value parseJobRow( const drogon::orm::Row& row )
{
	Json::Value json {};

	return json;
}

drogon::Task< drogon::HttpResponsePtr > getAllJobStatuses( drogon::orm::DbClientPtr db )
{
	const auto jobs { co_await db->execSqlCoro( "SELECT job_id FROM jobs" ) };

	Json::Value json {};
	json.resize( 0 );

	Json::ArrayIndex index { 0 };
	for ( const auto& row : jobs )
	{
		json[ index ] = co_await getJobJson( row[ "job_id" ].as< std::string >(), db );
		index++;
	}
	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

drogon::Task< drogon::HttpResponsePtr > getJobStatus( const std::string job_id, drogon::orm::DbClientPtr db )
{
	co_return drogon::HttpResponse::newHttpJsonResponse( co_await getJobJson( job_id, db ) );
}

drogon::Task< drogon::HttpResponsePtr > JobsAPI::jobStatus( const drogon::HttpRequestPtr request )
{
	// if there is no `job_id` parameter, assume we want all the jobs
	auto db { drogon::app().getDbClient() };
	const auto job_id { request->getOptionalParameter< std::string >( "job_id" ) };

	if ( job_id ) co_return co_await getJobStatus( *job_id, db );

	co_return co_await getAllJobStatuses( db );
}

} // namespace idhan::api