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
	std::size_t service_id;
	QString name;
	std::size_t num_mappings;
	std::size_t num_aliases;
	std::size_t num_parents;

	ServiceInfo() = default;
};

struct HydrusImporter
{
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

	void finish();

	void copyFileStorage();

	HydrusImporter() = delete;
	HydrusImporter( const std::filesystem::path& path );
	~HydrusImporter();

	bool hasPTR() const;

	std::vector< ServiceInfo > getTagServices();
};

} // namespace idhan::hydrus