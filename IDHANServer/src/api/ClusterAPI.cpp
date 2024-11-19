//
// Created by kj16609 on 11/15/24.
//
#include "ClusterAPI.hpp"

namespace idhan::api
{

ClusterInfo parseRow( const drogon::orm::Row& row )
{
	ClusterInfo info {};

	info.m_id = row[ 0 ].as< ClusterID >();
	info.ratio = row[ 1 ].as< std::uint16_t >();
	info.size.used = row[ 2 ].as< std::size_t >();
	info.size.limit = row[ 3 ].as< std::size_t >();
	info.file_count = row[ 4 ].as< std::size_t >();
	info.read_only = row[ 5 ].as< bool >();
	info.allowed_thumbnails = row[ 6 ].as< bool >();
	info.allowed_files = row[ 7 ].as< bool >();
	info.cluster_name = row[ 8 ].as< std::string >();
	info.path = row[ 9 ].as< std::string >();

	return info;
}

std::mutex mtx {};
std::unordered_map< ClusterID, ClusterInfo > clusters {};

void rebuildClusterInfo()
{
	std::lock_guard guard { mtx };
	clusters.clear();

	auto db { drogon::app().getDbClient() };

	const auto result {
		db->execSqlSync(
			"SELECT cluster_id, ratio_number, size_used, size_limit, file_count, read_only, allowed_thumbnails, allowed_files, cluster_name, folder_path FROM file_clusters" )
	};

	for ( std::size_t i = 0; i < result.size(); ++i )
	{
		auto row { result[ i ] };
		auto cluster_info { parseRow( row ) };
		clusters.insert( std::make_pair( cluster_info.m_id, std::move( cluster_info ) ) );
	}
}

} // namespace idhan::api
