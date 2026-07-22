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

//! Summary of a Hydrus tag service: its id, name, and mapping/alias/parent counts.
struct ServiceInfo
{
	std::size_t service_id { static_cast< std::size_t >( ~0 ) };
	QString name { "INVALID SERVICE" };
	std::size_t num_mappings { 0 };
	std::size_t num_aliases { 0 };
	std::size_t num_parents { 0 };

	ServiceInfo() = default;
};

//! A Hydrus-internal hash identifier (row id of a hash in the Hydrus database).
using HashID = std::uint32_t;

//! Drives a one-time import from a Hydrus SQLite database set (master, client and mappings DBs).
//! Opens the databases, copies the file storage into IDHAN, maps Hydrus hash IDs to IDHAN RecordIDs,
//! and enumerates the available tag services.
class HydrusImporter
{
	//! Opens a Hydrus SQLite database read-only, throwing a descriptive error if it cannot be opened.
	static void openDatabase( const std::filesystem::path& db_path, sqlite3** db );

  public:

	sqlite3* master_db { nullptr };
	sqlite3* client_db { nullptr };
	sqlite3* mappings_db { nullptr };
	std::filesystem::path m_path;

	FGL_DELETE_COPY( HydrusImporter );
	FGL_DELETE_MOVE( HydrusImporter );

	//! Copies the Hydrus file storage into IDHAN's clusters.
	//! TODO: intentional stub — not yet wired into the import flow (see on_parseHydrusDB_pressed).
	void copyFileStorage();

	HydrusImporter() = delete;
	HydrusImporter( const std::filesystem::path& path );
	~HydrusImporter();

	//! Maps a batch of Hydrus hash IDs to their corresponding IDHAN RecordIDs.
	std::unordered_map< HashID, RecordID > mapHydrusRecords( std::vector< HashID > hash_ids ) const;

	//! \return true if the Hydrus database contains PTR (public tag repository) data.
	bool hasPTR() const;

	//! \return Summaries of all tag services present in the Hydrus database.
	std::vector< ServiceInfo > getTagServices();
};

} // namespace idhan::hydrus