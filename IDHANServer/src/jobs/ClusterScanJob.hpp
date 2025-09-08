//
// Created by kj16609 on 9/8/25.
//
#pragma once
#include "IDHANTypes.hpp"
#include "JobContext.hpp"
#include "fgl/defines.hpp"

namespace idhan::jobs
{

class ClusterScanJob final : public JobContext
{
	bool m_scan_mime;
	bool m_rescan_mime;
	bool m_adopt_orphans;
	bool m_scan_metadata;
	bool m_rescan_metadata;
	bool m_recompute_hash;
	ClusterID m_cluster_id;

  public:

	FGL_DELETE_ALL_RO5( ClusterScanJob );

	explicit ClusterScanJob( const std::string& job_id, const Json::Value& json );

	drogon::Task< void > prepare( drogon::orm::DbClientPtr db ) override;
	drogon::Task< void > run() override;

	Json::Value serialize() override;
};

} // namespace idhan::jobs