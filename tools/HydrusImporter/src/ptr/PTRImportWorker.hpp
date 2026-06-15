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

class PTRImportWorker : public QObject, public QRunnable
{
	Q_OBJECT

  public:

	explicit PTRImportWorker( const std::filesystem::path& ptr_directory, QObject* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRImportWorker );
	~PTRImportWorker() override;
	void run() override;

	void requestCancel() { m_cancelled = true; }

  signals:

	void progress( const QString& status );
	void fileProcessed( int current, int total );
	void finished( bool success, const QString& message );

  private:

	void loadMetadata();
	bool processInOrder(); // returns true if cancelled mid-run

	void processSingleContentFile( const std::string& hash_hex, const ContentUpdate& content, idhan::TagDomainID domain_id );

	std::filesystem::path m_ptr_directory;
	TranslationTables m_tables;

	MetadataUpdate m_metadata;
	std::unordered_set< std::string > m_imported_hashes;

	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
