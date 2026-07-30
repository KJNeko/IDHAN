#pragma once

#include <QWidget>
#include <QtGlobal>

#include <memory>

namespace Ui
{
class PTRFlattenWidget;
}

namespace idhan::hydrus::ptr
{
class PTRFlattenWorker;
} // namespace idhan::hydrus::ptr

//! Runs the flatten pass over a downloaded PTR corpus and reports progress. On success it
//! announces the output directory so the Import tab can be pointed straight at it.
class PTRFlattenWidget : public QWidget
{
	Q_OBJECT

  public:

	explicit PTRFlattenWidget( QWidget* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRFlattenWidget )
	~PTRFlattenWidget() override;

  public slots:

	void setDirectory( const QString& path );

  signals:

	void outputDirectoryChanged( const QString& path );

  private slots:

	void onSourceEdited( const QString& path );
	void onSelectSource();
	void onSelectOutput();
	void onFlatten();
	void onCancel();
	void onProgress( const QString& status );
	void onSubProgress( int current, int total, const QString& status );
	void onStatsUpdated( quint64 eventsScanned,
	                     quint64 recordsFlattened,
	                     quint64 chainsCollapsed,
	                     quint64 terminalDeletes,
	                     quint64 chunksWritten,
	                     quint64 skippedFiles,
	                     quint64 skippedMissingDefinitions );
	void onFinished( bool success, const QString& message );

  private:

	//! The compacted subdirectory of \p source, which is where output goes by default.
	static QString defaultOutputFor( const QString& source );

	//! Zeroes the stats panel at the start of a run, so it never shows a stale count from a
	//! previous flatten.
	void resetStats();

	Ui::PTRFlattenWidget* ui;
	std::unique_ptr< idhan::hydrus::ptr::PTRFlattenWorker > m_worker {};
	bool m_flattening { false };

	//! Set once the user picks an output directory themselves, after which it stops tracking
	//! the source. Without this, choosing an output and then changing the source would silently
	//! discard the choice.
	bool m_output_overridden { false };
};
