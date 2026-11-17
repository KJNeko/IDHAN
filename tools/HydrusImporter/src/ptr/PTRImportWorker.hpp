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

#include "ptr/PTRFileParser.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/Manifest.hpp"

namespace idhan
{
class IDHANClient;
} // namespace idhan

namespace idhan::hydrus::ptr
{

//! PTR id→value lookup tables, accumulated from the definitions updates.
struct TranslationTables
{
	std::unordered_map< int, std::string > hash_id_to_sha256;
	std::unordered_map< int, std::string > tag_id_to_tag;
};

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

//! One row of PTR import history: the stats for a single completed PTR update batch.
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

//! Imports PTR updates in dependency order; supports concurrent downloads and cancellation.
class PTRImportWorker : public QObject, public QRunnable
{
	Q_OBJECT

  public:

	static constexpr std::size_t BATCH_SIZE { 128 };

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
		std::string state; //!< "running" | "done" | "error" | "cancelled"
		std::chrono::seconds heartbeat_age;
	};

	void loadMetadata();

	//! Returns true when cancelled before all updates are imported.
	bool processInOrder();

	DownloadStatus readDownloadStatus() const;

	//! Waits for a file or downloader shutdown, stall, or cancellation; true means the file exists.
	bool waitForFile( const std::filesystem::path& file_path );

	ContentStats processSingleContentFile(
		const std::string& hash_hex,
		const ContentUpdate& content,
		idhan::TagDomainID domain_id,
		const QString& progress_prefix,
		std::unordered_set< int >& unique_tag_ids );

	//! Looks up, or creates, the "public tag repository" tag domain.
	//! \throws std::runtime_error if it can be neither found nor created.
	static idhan::TagDomainID ensureTagDomain();

	//! Imports every chunk the manifest lists. Returns true if cancelled mid-run.
	bool processCompacted( idhan::TagDomainID domain_id );

	//! Returns IDs in chunk.strings order, creating missing tags; failed entries are zero.
	std::vector< idhan::TagID > resolveChunkTags( const Chunk& chunk, const QString& progress_prefix );

	ContentStats importChunk( const Chunk& chunk, idhan::TagDomainID domain_id, const QString& progress_prefix );

	void loadImportedChunks();
	void saveImportedChunks() const;

	std::filesystem::path m_ptr_directory;
	TranslationTables m_tables;

	MetadataUpdate m_metadata;
	std::unordered_set< std::string > m_imported_hashes;

	bool m_compacted { false };
	CompactManifest m_manifest;

	//! ptr_tag_id -> IDHAN TagID, 0 meaning not yet created.
	std::vector< idhan::TagID > m_ptr_tag_to_idhan;

	std::unordered_set< std::string > m_imported_chunks;

	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
