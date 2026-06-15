#pragma once

#include <QObject>
#include <QRunnable>

#include <IDHANTypes.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

	~PTRImportWorker() override;
	void run() override;

	// For cancellation from another thread
	void requestCancel() { m_cancelled = true; }

	bool isCancelled() const { return m_cancelled; }

  signals:

	void progress( const QString& status );
	void fileProcessed( const QString& hash_hex, int current, int total );
	void finished( bool success, const QString& message );

  private:

	struct FileEntry
	{
		std::filesystem::path path;
		std::string hash_hex; // filename without extension
		int update_index { -1 }; // from metadata, or -1 if unknown
		UpdateType type { UpdateType::Unknown };

		bool is_definitions() const { return type == UpdateType::Definitions; }

		bool is_content() const { return type == UpdateType::Content; }
	};

	void scanDirectory();
	void loadMetadataForOrdering();
	void buildTranslationTables();
	void processContentFiles();

	void processSingleContentFile( const FileEntry& entry, const ContentUpdate& content, idhan::TagDomainID domain_id );

	std::filesystem::path m_ptr_directory;
	TranslationTables m_tables;
	std::vector< FileEntry > m_file_entries;

	// For ordering
	MetadataUpdate m_metadata;
	std::unordered_set< std::string > m_imported_hashes; // hashes already imported in previous runs

	bool m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
