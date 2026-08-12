#pragma once

#include <QObject>
#include <QRunnable>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <filesystem>

#include "ptr/flatten/FlattenLiveStats.hpp"

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
		bool discard_terminal_deletes,
		QObject* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRFlattenWorker )
	~PTRFlattenWorker() override;

	void run() override;

	void requestCancel() { m_cancelled = true; }

  signals:

	void progress( const QString& status );
	void subProgress( int current, int total, const QString& status );
	//! Running counters, forwarded whole at the same cadence as subProgress.
	//!
	//! Passed as the struct rather than as a parameter per counter: there are nine of them and
	//! they were all the same type, so a transposed pair would have compiled and mislabelled the
	//! panel for a whole run. Adding a counter now costs nothing at this boundary.
	void statsUpdated( const idhan::hydrus::ptr::FlattenLiveStats& stats );
	void finished( bool success, const QString& message );

  private:

	std::filesystem::path m_ptr_directory;
	std::filesystem::path m_output_directory;
	bool m_discard_terminal_deletes;
	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
