#pragma once

#include <QWidget>

#include <filesystem>
#include <memory>

namespace idhan::hydrus::ptr
{
class PTRImportWorker;
}

namespace Ui
{
class PTRImportWidget;
}

class PTRImportWidget final : public QWidget
{
	Q_OBJECT

  public:

	explicit PTRImportWidget( QWidget* parent = nullptr );
	~PTRImportWidget() override;

	Q_DISABLE_COPY_MOVE( PTRImportWidget );

  public slots:

	void setDirectory( const QString& path );
	void onSelectDirectory();
	void onScan();
	void onImport();
	void onProgress( const QString& status );
	void onFileProcessed( const QString& hash_hex, int current, int total );
	void onImportFinished( bool success, const QString& message );

  private:

	Ui::PTRImportWidget* ui;
	std::filesystem::path m_directory;
	std::unique_ptr< idhan::hydrus::ptr::PTRImportWorker > m_worker;
	int m_total_files { 0 };
	int m_processed_files { 0 };
	bool m_importing { false };
};
