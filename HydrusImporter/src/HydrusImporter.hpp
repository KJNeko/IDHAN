//
// Created by kj16609 on 9/11/24.
//

#pragma once

#include <QFutureSynchronizer>
#include <QFutureWatcher>

#include <IDHAN>
#include <filesystem>
#include <sqlite3.h>

namespace idhan::hydrus
{

struct Set;

class HydrusImporter
{
	sqlite3* master_db { nullptr };
	sqlite3* client_db { nullptr };
	sqlite3* mappings_db { nullptr };
	std::shared_ptr< IDHANClient > m_client;
	std::filesystem::path m_path;
	QFuture< void > final_future;

	QFutureSynchronizer< void > sync {};
	std::size_t thread_count { 1 };
	bool m_process_ptr_mappings;
	const std::chrono::time_point< std::chrono::steady_clock > start_time { std::chrono::steady_clock::now() };

	void copyTagDomains();
	void copyDomainMappings( TagDomainID domain_id, std::size_t hy_service_id );

	void processSets( const std::vector< Set >& sets, TagDomainID domain_id );

	void copyTags();
	void copyParents();
	void copySiblings();

	void copyFileStorage();

	void copyHashes();

	void copyMappings();

  public:

	void finish();

	HydrusImporter() = delete;
	HydrusImporter(
		const std::filesystem::path& path, std::shared_ptr< IDHANClient >& client, const bool process_ptr_flag );
	~HydrusImporter();

	void copyHydrusTags();

	void copyFileInfo();

	void copyHydrusInfo();
};

} // namespace idhan::hydrus