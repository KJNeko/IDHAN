#pragma once

#include <QWidget>

#include <chrono>

#include "HydrusImporter.hpp"
#include "TagServiceWorker.hpp"
#include "gui/hydrus/ImportProgress.hpp"
#include "gui/hydrus/ImportServiceStatus.hpp"

namespace Ui
{
class TagServiceWidget;
}

//! Qt widget presenting a single Hydrus tag service and its import progress (driven by TagServiceWorker).
class TagServiceWidget : public QWidget
{
	Q_OBJECT

	std::size_t mappings_processed { 0 };
	std::size_t parents_processed { 0 };
	std::size_t aliases_processed { 0 };
	std::size_t records_processed { 0 };
	idhan::hydrus::ServiceInfo m_info;
	QString m_name;
	bool m_preprocessed { false };
	TagServiceWorker* m_worker;
	idhan::hydrus::ImportServiceStatus* m_status { nullptr };
	QTimer m_time_timer { this };
	bool m_importing { false };
	using Clock = idhan::hydrus::ImportProgress::Clock;
	using TimePoint = Clock::time_point;
	TimePoint m_start;

	std::optional< TagServiceWorker::Phase > m_phase {};
	idhan::hydrus::ImportProgress m_progress {};

  public:

	Q_DISABLE_COPY_MOVE( TagServiceWidget )

	explicit TagServiceWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent = nullptr );
	~TagServiceWidget() override;

	void setName( const QString& name );
	void updateTime();

	bool ready() const { return m_preprocessed; }

	void setInfo( const idhan::hydrus::ServiceInfo& service_info );

  signals:
	void preprocessingComplete( bool success );

  public slots:
	void startImport();
	void startPreImport();
	void phaseStarted( TagServiceWorker::Phase phase, std::chrono::steady_clock::time_point started );
	void phaseProgress( std::size_t count, std::chrono::steady_clock::time_point completed );

	void updateProcessed();

	void processedMappings( std::size_t count, std::size_t record_count );
	void processedParents( std::size_t count );
	void processedAliases( std::size_t count );

	void preprocessingFinished( bool success );
	void importFinished();
	void importFailed( const QString& message );
	void setMaxMappings( std::size_t count );
	void setMaxParents( std::size_t count );
	void setMaxAliases( std::size_t count );

  private:

	Ui::TagServiceWidget* ui;
};
