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

struct ServiceInfo
{
	std::size_t service_id { static_cast< std::size_t >( ~0 ) };
	QString name { "INVALID SERVICE" };
	std::size_t num_mappings { 0 };
	std::size_t num_aliases { 0 };
	std::size_t num_parents { 0 };

	ServiceInfo() = default;
};

using HashID = std::uint32_t;

class HydrusImporter
{
  public:

	sqlite3* master_db { nullptr };
	sqlite3* client_db { nullptr };
	sqlite3* mappings_db { nullptr };
	std::filesystem::path m_path;
	QFuture< void > final_future;

	QFutureSynchronizer< void > sync {};
	std::size_t thread_count { 1 };
	bool m_process_ptr_mappings;
	const std::chrono::time_point< std::chrono::steady_clock > start_time { std::chrono::steady_clock::now() };

	FGL_DELETE_COPY( HydrusImporter );
	FGL_DELETE_MOVE( HydrusImporter );

	void copyFileStorage();

	HydrusImporter() = delete;
	HydrusImporter( const std::filesystem::path& path );
	~HydrusImporter();

	std::unordered_map< HashID, RecordID > mapHydrusRecords( std::vector< HashID > hash_ids ) const;
	RecordID getRecordIDFromHyID( HashID hash_id );

	bool hasPTR() const;

	std::vector< ServiceInfo > getTagServices();
};

} // namespace idhan::hydrus