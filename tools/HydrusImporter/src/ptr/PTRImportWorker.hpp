#pragma once

#include <QObject>
#include <QRunnable>

#include <IDHANTypes.hpp>
#include <atomic>
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

struct TranslationTables
{
	std::unordered_map< int, std::string > hash_id_to_sha256; // service_hash_id → hex SHA-256
	std::unordered_map< int, std::string > tag_id_to_tag; // service_tag_id → "namespace:subtag"
};

struct ContentStats
{
	int records_created = 0;
	int tags_added = 0;
	int tags_removed = 0;
	int parents_added = 0;
	int parents_removed = 0;
	int aliases_added = 0;
	int aliases_removed = 0;
};

class PTRImportWorker : public QObject, public QRunnable
{
	Q_OBJECT

  public:

	explicit PTRImportWorker( const std::filesystem::path& ptr_directory, QObject* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRImportWorker );
	~PTRImportWorker() override;
	void run() override;

	void requestCancel() { m_cancelled = true; }

	void setBatchSize( std::size_t batch_size ) { m_batch_size = batch_size; }

	[[nodiscard]] std::size_t batchSize() const { return m_batch_size; }

  signals:

	void progress( const QString& status );
	void subProgress( int current, int total, const QString& status );
	void fileProcessed( int current, int total );
	void updateCompleted( const QString& summary );
	void finished( bool success, const QString& message );

  private:

	void loadMetadata();
	bool processInOrder(); // returns true if cancelled mid-run

	ContentStats processSingleContentFile(
		const std::string& hash_hex,
		const ContentUpdate& content,
		idhan::TagDomainID domain_id,
		const QString& progress_prefix );

	std::filesystem::path m_ptr_directory;
	TranslationTables m_tables;

	MetadataUpdate m_metadata;
	std::unordered_set< std::string > m_imported_hashes;
	std::size_t m_batch_size { 250 };

	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
