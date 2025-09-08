//
// Created by kj16609 on 9/8/25.
//
#include "ClusterScanJob.hpp"

#include "FileScanJob.hpp"
#include "JobContext.hpp"
#include "filesystem/ClusterManager.hpp"

namespace idhan::jobs
{

ClusterScanJob::ClusterScanJob( const std::string& job_id, const Json::Value& json ) :
  JobContext( job_id ),
  m_scan_mime( false ),
  m_rescan_mime( false ),
  m_adopt_orphans( false ),
  m_scan_metadata( false ),
  m_rescan_metadata( false ),
  m_recompute_hash( false ),
  m_cluster_id( 0 )
{
	//{"version": 1, "scan_mime": true, "cluster_id": 1, "rescan_mime": false, "adopt_orphans": false, "scan_metadata": true, "recompute_hash": true, "rescan_metadata": false}
	const auto version { json.get( "version", 1 ).asInt() };

	switch ( version )
	{
		case 1:
			m_scan_mime = json.get( "scan_mime", false ).asBool();
			m_rescan_mime = json.get( "rescan_mime", false ).asBool();
			m_adopt_orphans = json.get( "adopt_orphans", false ).asBool();
			m_scan_metadata = json.get( "scan_metadata", false ).asBool();
			m_rescan_metadata = json.get( "rescan_metadata", false ).asBool();
			m_recompute_hash = json.get( "recompute_hash", false ).asBool();
			m_cluster_id = static_cast< ClusterID >( json.get( "cluster_id", 0 ).asInt() );
			break;
		default:
			throw std::runtime_error( "Unsupported version in ClusterScanJob" );
	}
}

drogon::Task< void > ClusterScanJob::prepare( drogon::orm::DbClientPtr db )
{
	// start by getting every single path for the given cluster id

	const auto cluster_records {
		co_await db->execSqlCoro( "SELECT record_id FROM file_info WHERE cluster_id = $1", m_cluster_id )
	};

	Json::Value template_json {};
	template_json[ "scan_mime" ] = m_scan_mime;
	template_json[ "rescan_mime" ] = m_rescan_mime;
	template_json[ "scan_metadata" ] = m_scan_metadata;
	template_json[ "rescan_metadata" ] = m_rescan_metadata;
	template_json[ "recompute_hash" ] = m_recompute_hash;

	// Prepare a new job for each cluster id.
	for ( const auto& row : cluster_records )
	{
		const auto record_id { row[ 0 ].as< RecordID >() };

		Json::Value json { template_json };
		json[ "record_id" ] = record_id;

		const auto job_ctx { co_await createJob< FileScanJob >( json ) };

		co_await this->addDependency( job_ctx, db );
	}
}

drogon::Task< void > ClusterScanJob::run()
{}

Json::Value ClusterScanJob::serialize()
{
	Json::Value json;
	json[ "version" ] = 1;
	json[ "scan_mime" ] = m_scan_mime;
	json[ "rescan_mime" ] = m_rescan_mime;
	json[ "adopt_orphans" ] = m_adopt_orphans;
	json[ "scan_metadata" ] = m_scan_metadata;
	json[ "rescan_metadata" ] = m_rescan_metadata;
	json[ "recompute_hash" ] = m_recompute_hash;
	json[ "cluster_id" ] = static_cast< int >( m_cluster_id );
	return json;
}

} // namespace idhan::jobs