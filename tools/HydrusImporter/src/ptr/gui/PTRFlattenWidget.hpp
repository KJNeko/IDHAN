#pragma once

#include <QWidget>
#include <QtGlobal>

#include <memory>

#include "ptr/flatten/FlattenLiveStats.hpp"

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
	void onStatsUpdated( const idhan::hydrus::ptr::FlattenLiveStats& stats );
	void onFinishedSync( bool success, const QString& message );

  private:
	//! The corpus's COMPACT_SUBDIRECTORY, or empty when \p source is empty.
	static QString defaultOutputFor( const QString& source );

	void resetStats();

	Ui::PTRFlattenWidget* ui;
	std::unique_ptr< idhan::hydrus::ptr::PTRFlattenWorker > m_worker {};
	bool m_flattening { false };

	//! Set once the user picks an output directory, after which it stops tracking the source.
	bool m_output_overridden { false };
};
