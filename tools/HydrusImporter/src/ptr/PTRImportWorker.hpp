#pragma once

#include <QMetaType>
#include <QObject>
#include <QRunnable>

#include <IDHANTypes.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "PTRFileParser.hpp"

namespace idhan
{
class IDHANClient;
} // namespace idhan

namespace idhan::hydrus::ptr
{

//! PTR id→value lookup tables, accumulated from the definitions updates.
struct TranslationTables
{
	std::unordered_map< int, std::string > hash_id_to_sha256; // service_hash_id → hex SHA-256
	std::unordered_map< int, std::string > tag_id_to_tag; // service_tag_id → "namespace:subtag"
};

//! Running counts of what applying a content update produced.
struct ContentStats
{
	int records_created = 0;
	int tags_created = 0;
	int mappings_added = 0;
	int mappings_removed = 0;
	int parents_added = 0;
	int parents_removed = 0;
	int aliases_added = 0;
	int aliases_removed = 0;
};

//! One row of PTR import history — the stats for a single completed PTR update batch.
struct PTRHistoryEntry
{
	int update_index = 0;
	int64_t file_count = 0;
	ContentStats stats;
};

} // namespace idhan::hydrus::ptr

Q_DECLARE_METATYPE( idhan::hydrus::ptr::PTRHistoryEntry )

namespace idhan::hydrus::ptr
{

//! QRunnable that imports downloaded PTR update files from a directory into IDHAN, applying them in
//! dependency order and in batches. Cancellable via requestCancel(); progress via the signals below.
class PTRImportWorker : public QObject, public QRunnable
{
	Q_OBJECT

  public:

	// Rows per request. There is deliberately no cap on how many batches are in flight at once:
	// requests are handed straight to QNetworkAccessManager, which throttles the real socket count
	// per host, so we submit everything and let it pipeline.
	static constexpr std::size_t BATCH_SIZE { 128 };

	// How often to re-check for a not-yet-downloaded file, and how long to keep waiting on one
	// with no progress from the downloader before giving up and treating it as a real gap.
	static constexpr std::chrono::seconds FILE_WAIT_POLL_INTERVAL { 3 };
	static constexpr std::chrono::seconds FILE_WAIT_STALL_TIMEOUT { 180 };

	explicit PTRImportWorker( const std::filesystem::path& ptr_directory, QObject* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRImportWorker )
	~PTRImportWorker() override;
	void run() override;

	void requestCancel() { m_cancelled = true; }

  signals:

	void progress( const QString& status );
	void subProgress( int current, int total, const QString& status );
	void fileProcessed( int current, int total );
	void updateCompleted( const PTRHistoryEntry& entry );
	void finished( bool success, const QString& message );

  private:

	//! The downloader's last-known progress state, read from ptr_metadata.json's "state" field
	//! (or inferred as "running" from metadata.ptrupdate's mtime if that file doesn't exist yet).
	struct DownloadStatus
	{
		std::string state; // "running" | "done" | "error" | "cancelled"
		std::chrono::seconds heartbeat_age;
	};

	void loadMetadata();
	bool processInOrder(); // returns true if cancelled mid-run
	DownloadStatus readDownloadStatus() const;
	//! Blocks (polling) until file_path exists, the downloader reports a terminal state, its
	//! heartbeat goes stale, or cancellation is requested. Returns true iff the file now exists.
	bool waitForFile( const std::filesystem::path& file_path );

	ContentStats processSingleContentFile(
		const std::string& hash_hex,
		const ContentUpdate& content,
		idhan::TagDomainID domain_id,
		const QString& progress_prefix,
		std::unordered_set< int >& unique_tag_ids );

	std::filesystem::path m_ptr_directory;
	TranslationTables m_tables;

	MetadataUpdate m_metadata;
	std::unordered_set< std::string > m_imported_hashes;

	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
