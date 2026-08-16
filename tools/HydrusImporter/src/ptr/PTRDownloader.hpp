#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "ptr/PTRFileParser.hpp"

namespace idhan::hydrus::ptr
{

//! Downloads PTR (public tag repository) update files from a Hydrus PTR server into an output
//! directory, resuming from previously cached metadata and rate-limiting requests. Drive it with
//! startSync(); progress and completion are reported via the signals below. Writes its progress
//! and state incrementally to ptr_metadata.json (index-by-index, plus a periodic heartbeat) so a
//! PTRImportWorker pointed at the same directory can import concurrently instead of waiting for
//! the whole sync to finish.
class PTRDownloader : public QObject
{
	Q_OBJECT

  public:

	//! Current stage of the download.
	enum class State
	{
		Idle,
		DownloadingMetadata,
		DownloadingUpdates,
		Saving,
		Done,
		Error
	};

	PTRDownloader(
		const std::filesystem::path& output_dir,
		QString host = "ptr.hydrus.network",
		quint16 port = 45871,
		QString access_key = "4a285629721ca442541ef2c15ea17d1f7f7578b0c3f4f5f2a05f8f0ab297786f",
		QObject* parent = nullptr );

	~PTRDownloader() override;

	void startSync();
	void cancel();

	State state() const { return m_state; }

	int totalFiles() const { return m_total_downloads; }

	int downloadedFiles() const { return m_completed_downloads; }

	int lastUpdateIndex() const { return m_last_update_index; }

  signals:

	void metadataReceived( int total_updates, int total_files );
	void fileDownloading( const QString& hash_hex, int current, int total );
	void fileDownloaded( const QString& hash_hex, int current, int total );
	void progress( const QString& status );
	void finished( bool success, const QString& message );

  private slots:

	void onMetadataReply( QNetworkReply* reply );
	void onUpdateReply( QNetworkReply* reply, QString hash_hex );

  private:

	void loadMetadata();
	void saveMetadata();
	QUrl makeUrl( const QString& subpath ) const;
	void downloadMetadata();
	bool updateExists( const std::string& value );
	void downloadNextUpdate();
	void processMetadataResponse( const QByteArray& data );
	MetadataUpdate parseMetadataBytes( const QByteArray& data );
	bool loadCachedMetadata();
	void buildDownloadQueue();
	void processUpdateResponse( const QString& hash_hex, const QByteArray& data );

	static constexpr const char* METADATA_FILENAME = "metadata.ptrupdate";
	static constexpr std::chrono::hours METADATA_MAX_AGE { 24 };
	static constexpr int HEARTBEAT_INTERVAL_MS = 30'000;

	QNetworkAccessManager m_network;
	std::filesystem::path m_output_dir;
	QString m_host;
	quint16 m_port;
	QString m_access_key;

	State m_state { State::Idle };
	bool m_cancelled { false };

	QString m_persist_state { "running" };
	QTimer m_heartbeat_timer;

	// Metadata tracking
	MetadataUpdate m_metadata;
	int m_last_update_index { -1 };
	std::set< std::string > m_downloaded_hashes;

	// Download queue
	std::vector< std::string > m_pending_downloads;
	int m_current_download_index { 0 };
	int m_total_downloads { 0 };
	int m_completed_downloads { 0 };

	std::unordered_map< std::string, int > m_hash_to_index;
	std::unordered_map< int, int > m_remaining_for_index;

	// Rate limiting: 5s fixed delay between downloads
};

} // namespace idhan::hydrus::ptr
