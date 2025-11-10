//
// Created by kj16609 on 6/28/25.
//
#pragma once

#include <QWidget>

#include <chrono>
#include <deque>

#include "HydrusImporter.hpp"

class TagServiceWorker;

namespace Ui
{
class TagServiceWidget;
}

class TagServiceWidget : public QWidget
{
	std::size_t mappings_processed { 0 };
	std::size_t parents_processed { 0 };
	std::size_t aliases_processed { 0 };
	std::size_t records_processed { 0 };
	idhan::hydrus::ServiceInfo m_info;
	QString m_name;
	bool m_preprocessed { false };
	TagServiceWorker* m_worker;
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = std::chrono::high_resolution_clock::time_point;
	TimePoint m_start;

	// Rate tracking
	struct ProcessingRecord
	{
		TimePoint timestamp;
		std::size_t count;
	};

	std::deque< ProcessingRecord > m_mapping_records;

  public:

	Q_DISABLE_COPY_MOVE( TagServiceWidget )

	explicit TagServiceWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent = nullptr );
	~TagServiceWidget() override;

	void setName( const QString& name );
	void updateTime();

	bool ready() const { return m_preprocessed; }

	void setInfo( const idhan::hydrus::ServiceInfo& service_info );

	// Rate calculation methods
	double getMappingsPerSecond() const { return getAverageMappingsPerMinute() / 60.0; }

	double getAverageMappingsPerMinute() const;

  public slots:
	void startImport();
	void startPreImport();

	void updateProcessed();

	void processedMappings( std::size_t count, std::size_t record_count );
	void processedParents( std::size_t count );
	void processedAliases( std::size_t count );

	void preprocessingFinished();
	void setMaxMappings( std::size_t count );
	void setMaxParents( std::size_t count );
	void setMaxAliases( std::size_t count );

  private:

	void recordMappingProcessed( std::size_t count );
	void cleanOldRecords();

	Ui::TagServiceWidget* ui;
};
