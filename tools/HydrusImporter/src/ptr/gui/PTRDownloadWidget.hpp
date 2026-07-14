#pragma once

#include <QWidget>

#include <filesystem>

namespace idhan::hydrus::ptr
{
class PTRDownloader;
}

namespace Ui
{
class PTRDownloadWidget;
}

//! Qt widget driving PTR update-file downloads (wraps PTRDownloader) and showing progress.
class PTRDownloadWidget final : public QWidget
{
	Q_OBJECT

  public:

	explicit PTRDownloadWidget( QWidget* parent = nullptr );
	~PTRDownloadWidget() override;

	Q_DISABLE_COPY_MOVE( PTRDownloadWidget );

  signals:

	void directoryChanged( const QString& path );

  public slots:

	void onSelectDirectory();
	void onDownload();
	void onMetadataReceived( int total_updates, int total_files );
	void onFileDownloading( const QString& hash_hex, int current, int total );
	void onFileDownloaded( const QString& hash_hex, int current, int total );
	void onDownloadFinished( bool success, const QString& message );
	void onProgress( const QString& status );

  private:

	Ui::PTRDownloadWidget* ui;
	std::unique_ptr< idhan::hydrus::ptr::PTRDownloader > m_downloader;
	std::filesystem::path m_directory;
	bool m_downloading { false };
};
