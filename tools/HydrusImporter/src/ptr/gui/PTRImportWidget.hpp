#pragma once

#include <QWidget>

#include <memory>

namespace idhan::hydrus::ptr
{
class PTRImportWorker;
}

namespace Ui
{
class PTRImportWidget;
}

//! Qt widget driving the import of downloaded PTR update files (wraps PTRImportWorker) and showing progress.
class PTRImportWidget final : public QWidget
{
	Q_OBJECT

  public:

	explicit PTRImportWidget( QWidget* parent = nullptr );
	~PTRImportWidget() override;

	Q_DISABLE_COPY_MOVE( PTRImportWidget )

  public slots:

	void setDirectory( const QString& path );
	void onSelectDirectory();
	void onImport();
	void onCancel();
	void onProgress( const QString& status );
	void onSubProgress( int current, int total, const QString& status );
	void onFileProcessed( int current, int total );
	void onUpdateCompleted( const QString& summary );
	void onImportFinished( bool success, const QString& message );

  private:

	Ui::PTRImportWidget* ui;
	std::unique_ptr< idhan::hydrus::ptr::PTRImportWorker > m_worker;
	bool m_importing { false };
};
