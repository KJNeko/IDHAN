#pragma once

#include <QObject>
#include <QRunnable>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <filesystem>

namespace idhan::hydrus::ptr
{

//! QRunnable wrapper around runFlatten. Deliberately thin: every decision lives in PTRCore, so
//! the pipeline stays testable without Qt.
class PTRFlattenWorker : public QObject, public QRunnable
{
	Q_OBJECT

  public:

	PTRFlattenWorker( std::filesystem::path ptr_directory,
	                  std::filesystem::path output_directory,
	                  QObject* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRFlattenWorker )
	~PTRFlattenWorker() override;

	void run() override;

	void requestCancel() { m_cancelled = true; }

  signals:

	void progress( const QString& status );
	void subProgress( int current, int total, const QString& status );
	//! Running counters, forwarded from FlattenLiveStats at the same cadence as subProgress.
	void statsUpdated( quint64 eventsScanned,
	                   quint64 recordsFlattened,
	                   quint64 chainsCollapsed,
	                   quint64 terminalDeletes,
	                   quint64 chunksWritten,
	                   quint64 skippedFiles,
	                   quint64 skippedMissingDefinitions );
	void finished( bool success, const QString& message );

  private:

	std::filesystem::path m_ptr_directory;
	std::filesystem::path m_output_directory;
	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
