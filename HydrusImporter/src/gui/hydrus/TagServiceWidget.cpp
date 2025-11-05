//
// Created by kj16609 on 6/28/25.
//
#include "TagServiceWidget.hpp"

#include <QWidget>

#include <iostream>

#include "TagServiceWorker.hpp"
#include "ui_TagServiceWidget.h"

TagServiceWidget::TagServiceWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent ) :
  QWidget( parent ),
  m_info(),
  m_name(),
  m_worker( new TagServiceWorker( this, importer ) ),
  ui( new Ui::TagServiceWidget )
{
	ui->setupUi( this );

	m_worker->setService( m_info );

	connect(
		m_worker,
		&TagServiceWorker::processedMappings,
		this,
		&TagServiceWidget::processedMappings,
		Qt::AutoConnection );
	connect(
		m_worker, &TagServiceWorker::processedParents, this, &TagServiceWidget::processedParents, Qt::AutoConnection );
	connect(
		m_worker, &TagServiceWorker::processedAliases, this, &TagServiceWidget::processedAliases, Qt::AutoConnection );
	connect(
		m_worker,
		&TagServiceWorker::processedMaxMappings,
		this,
		&TagServiceWidget::setMaxMappings,
		Qt::AutoConnection );
	connect(
		m_worker, &TagServiceWorker::processedMaxParents, this, &TagServiceWidget::setMaxParents, Qt::AutoConnection );
	connect(
		m_worker, &TagServiceWorker::processedMaxAliases, this, &TagServiceWidget::setMaxAliases, Qt::AutoConnection );
	connect(
		m_worker, &TagServiceWorker::finished, this, &TagServiceWidget::preprocessingFinished, Qt::AutoConnection );
}

TagServiceWidget::~TagServiceWidget()
{
	delete ui;
}

void TagServiceWidget::setName( const QString& name )
{
	m_name = name;
	ui->name->setText( QString( "Name: %L1" ).arg( name ) );
}

void TagServiceWidget::recordMappingProcessed( std::size_t count )
{
	const auto now = Clock::now();

	m_mapping_records.emplace_back( now, count );

	// Clean old records (older than 1 minute)
	cleanOldRecords();
}

void TagServiceWidget::cleanOldRecords()
{
	const auto now = Clock::now();
	const auto one_minute_ago = now - std::chrono::minutes( 1 );

	while ( !m_mapping_records.empty() && m_mapping_records.front().timestamp < one_minute_ago )
	{
		m_mapping_records.pop_front();
	}
}

double TagServiceWidget::getAverageMappingsPerMinute() const
{
	if ( m_mapping_records.empty() ) return 0.0;

	std::size_t total_mappings = 0;

	// Sum up all complete seconds
	for ( const auto& record : m_mapping_records )
	{
		total_mappings += record.count;
	}

	// Calculate time span
	const auto now = Clock::now();
	auto time_span = std::chrono::milliseconds( 0 );

	if ( !m_mapping_records.empty() )
	{
		time_span =
			std::chrono::duration_cast< std::chrono::milliseconds >( now - m_mapping_records.front().timestamp );
	}

	if ( time_span.count() > 0 )
	{
		// Convert to mappings per minute
		return static_cast< double >( total_mappings ) / ( static_cast< double >( time_span.count() ) / 60000.0 );
	}

	return 0.0;
}

void TagServiceWidget::updateTime()
{
	const std::size_t to_process { m_info.num_mappings + m_info.num_parents + m_info.num_aliases };
	const std::size_t total_processed { mappings_processed + parents_processed + aliases_processed };

	// const bool over_limit { to_process > std::numeric_limits< int >::max() };
	// const std::size_t multip { over_limit ? 16 : 1 };

	if ( total_processed == -1 ) return;

	const auto time_elapsed {
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::high_resolution_clock::now() - m_start )
			.count()
	};

	if ( to_process == total_processed )
	{
		const auto hours = time_elapsed / 3600000;
		const auto minutes = ( time_elapsed % 3600000 ) / 60000;
		const auto seconds = ( time_elapsed % 60000 ) / 1000;
		ui->statusLabel->setText(
			QString( "Finished: %1:%2:%3" )
				.arg( hours, 2, 10, QChar( '0' ) )
				.arg( minutes, 2, 10, QChar( '0' ) )
				.arg( seconds, 2, 10, QChar( '0' ) ) );
		return;
	}

	const double ms_per_mapping { static_cast< double >( time_elapsed ) / static_cast< double >( total_processed ) };

	const auto time_remaining { static_cast< std::size_t >( ( to_process - total_processed ) * ms_per_mapping ) };

	const auto hours = time_remaining / 3600000;
	const auto minutes = ( time_remaining % 3600000 ) / 60000;
	const auto seconds = ( time_remaining % 60000 ) / 1000;

	// Add rate information to the status
	const double current_rate = getMappingsPerSecond();
	const double avg_rate = getAverageMappingsPerMinute();

	ui->statusLabel->setText(
		QString( "ETA: %1:%2:%3 | Rate: %L4/s | Avg: %L5/min" )
			.arg( hours, 2, 10, QChar( '0' ) )
			.arg( minutes, 2, 10, QChar( '0' ) )
			.arg( seconds, 2, 10, QChar( '0' ) )
			// .arg( total_processed, 2, 10, QChar( '0' ) )
			// .arg( to_process, 2, 10, QChar( '0' ) )
			.arg( static_cast< int >( current_rate ) )
			.arg( static_cast< int >( avg_rate ) )

	);
}

void TagServiceWidget::updateProcessed()
{
	const auto to_process { m_info.num_mappings + m_info.num_parents + m_info.num_aliases };
	const auto total_processed { mappings_processed + parents_processed + aliases_processed };

	const auto over_limit { to_process > std::numeric_limits< int >::max() };
	const auto multip { over_limit ? 1024 : 1 };

	ui->progressBar->setValue( static_cast< int >( total_processed / multip ) );
	ui->progressBar->setMaximum( static_cast< int >( to_process / multip ) );
}

void TagServiceWidget::processedMappings( std::size_t count, std::size_t record_count )
{
	mappings_processed += count;
	records_processed += record_count;
	recordMappingProcessed( count );

	QLocale locale { QLocale::English, QLocale::UnitedStates };
	locale.setNumberOptions( QLocale::DefaultNumberOptions );
	ui->mappingsCount->setText(
		QString( "Mappings: %L1 (%L2 processed)\nRecords: (%L3 processed)" )
			.arg( m_info.num_mappings )
			.arg( mappings_processed )
			.arg( records_processed ) );

	updateProcessed();
	updateTime();
}

void TagServiceWidget::processedParents( std::size_t count )
{
	parents_processed += count;
	ui->parentsCount->setText(
		QString( "Parents: %L1 (%L2 processed)" ).arg( m_info.num_parents ).arg( parents_processed ) );

	updateProcessed();
	updateTime();
}

void TagServiceWidget::processedAliases( std::size_t count )
{
	aliases_processed += count;
	ui->aliasesCount->setText(
		QString( "Aliases: %L1 (%L2 processed)" ).arg( m_info.num_aliases ).arg( aliases_processed ) );

	updateProcessed();
	updateTime();
}

void TagServiceWidget::preprocessingFinished()
{
	m_preprocessed = true;
	updateTime();
}

void TagServiceWidget::setMaxMappings( std::size_t count )
{
	m_info.num_mappings = count;
	if ( mappings_processed > 0 )
		ui->mappingsCount->setText(
			QString( "Mappings: %L1 (%L2 processed)\nRecords: (%L3 processed)" )
				.arg( m_info.num_mappings )
				.arg( mappings_processed )
				.arg( records_processed ) );
	else
		ui->mappingsCount->setText( QString( "Mappings: %L1" ).arg( m_info.num_mappings ) );
}

void TagServiceWidget::setMaxParents( std::size_t count )
{
	m_info.num_parents = count;
	if ( parents_processed > 0 )
		ui->parentsCount->setText(
			QString( "Parents: %L1 (%L2 processed)" ).arg( m_info.num_parents ).arg( parents_processed ) );
	else
		ui->parentsCount->setText( QString( "Parents: %L1" ).arg( m_info.num_parents ) );
}

void TagServiceWidget::setMaxAliases( std::size_t count )
{
	m_info.num_aliases = count;
	if ( aliases_processed > 0 )
		ui->aliasesCount->setText(
			QString( "Aliases: %L1 (%L2 processed)" ).arg( m_info.num_aliases ).arg( aliases_processed ) );
	else
		ui->aliasesCount->setText( QString( "Aliases: %L1" ).arg( m_info.num_aliases ) );
}

void TagServiceWidget::setInfo( const idhan::hydrus::ServiceInfo& service_info )
{
	m_info = service_info;
	m_worker->setService( m_info );
}

void TagServiceWidget::startImport()
{
	if ( ui->cbShouldImport->isChecked() ) QThreadPool::globalInstance()->start( m_worker );
	m_start = std::chrono::high_resolution_clock::now();
}

void TagServiceWidget::startPreImport()
{
	QThreadPool::globalInstance()->start( m_worker );
}