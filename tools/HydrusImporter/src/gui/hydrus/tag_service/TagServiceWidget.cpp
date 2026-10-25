#include "TagServiceWidget.hpp"

#include <QWidget>

#include <cmath>

#include "TagServiceWorker.hpp"
#include "ui_TagServiceWidget.h"

namespace idhan::hydrus
{

QString formatImportRate( const double rate )
{
	if ( rate > 0 && rate < 0.001 ) return "<0.001";

	return QLocale().toString( rate, 'f', rate > 0 && rate < 1 ? 3 : 1 );
}

} // namespace idhan::hydrus

TagServiceWidget::TagServiceWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent ) :
  QWidget( parent ),
  m_info(),
  m_name(),
  m_worker( new TagServiceWorker( this, importer ) ),
  m_start(),
  ui( new Ui::TagServiceWidget )
{
	ui->setupUi( this );
	ui->statusLabel->setToolTip(
		"ETA covers the current phase only. Rate is source entries processed per second over the last minute; "
		"Avg is source entries per minute since this phase began. Completed batches include their full processing "
		"and server wait time. Skipped or failed entries count as processed work and remain listed in the operation status." );
	m_status = new idhan::hydrus::ImportServiceStatus( ui->operationLabel, ui->timingLabel, this );
	m_time_timer.setInterval( 1000 );
	connect( &m_time_timer, &QTimer::timeout, this, &TagServiceWidget::updateTime );
	connect( m_worker, &TagServiceWorker::phaseStarted, this, &TagServiceWidget::phaseStarted );
	connect( m_worker, &TagServiceWorker::phaseProgress, this, &TagServiceWidget::phaseProgress );
	connect(
		m_worker,
		&TagServiceWorker::statusMessage,
		m_status,
		&idhan::hydrus::ImportServiceStatus::setOperation,
		Qt::QueuedConnection );
	connect(
		m_worker,
		&TagServiceWorker::warningOccurred,
		m_status,
		&idhan::hydrus::ImportServiceStatus::warning,
		Qt::QueuedConnection );

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
	connect( m_worker, &TagServiceWorker::preprocessingFinished, this, &TagServiceWidget::preprocessingFinished );
	connect( m_worker, &TagServiceWorker::finished, this, &TagServiceWidget::importFinished );
	connect( m_worker, &TagServiceWorker::errorOccurred, this, &TagServiceWidget::importFailed, Qt::AutoConnection );
}

TagServiceWidget::~TagServiceWidget()
{
	delete ui;
}

void TagServiceWidget::setName( const QString& name )
{
	m_name = name;
	ui->name->setText( QString( "Name: %L1\nType: Tag Service" ).arg( name ) );
}

void TagServiceWidget::phaseStarted( const TagServiceWorker::Phase phase, const Clock::time_point started )
{
	m_phase = phase;
	std::size_t total { 0 };
	switch ( phase )
	{
		case TagServiceWorker::Phase::Mappings:
			total = m_info.num_mappings;
			break;
		case TagServiceWorker::Phase::Aliases:
			total = m_info.num_aliases;
			break;
		case TagServiceWorker::Phase::Parents:
			total = m_info.num_parents;
			break;
		case TagServiceWorker::Phase::PreparingRelationships:
			break;
	}
	m_progress.start( total, started );
	updateTime();
}

void TagServiceWidget::phaseProgress( const std::size_t count, const Clock::time_point completed )
{
	if ( !m_importing ) return;

	m_progress.advance( count, completed );
	if ( count > 0 ) m_status->recordProgress();
	updateTime();
}

void TagServiceWidget::updateTime()
{
	if ( !m_importing ) return;

	if ( !m_phase )
	{
		ui->statusLabel->setText( "ETA: waiting to start" );
		return;
	}

	if ( *m_phase == TagServiceWorker::Phase::PreparingRelationships )
	{
		ui->statusLabel->setText( "ETA: estimating after relationship preparation" );
		return;
	}

	QString phase_name;
	switch ( *m_phase )
	{
		case TagServiceWorker::Phase::Mappings:
			phase_name = "Mappings";
			break;
		case TagServiceWorker::Phase::Aliases:
			phase_name = "Aliases";
			break;
		case TagServiceWorker::Phase::Parents:
			phase_name = "Parents";
			break;
		case TagServiceWorker::Phase::PreparingRelationships:
			return;
	}

	const auto progress = m_progress.snapshot();
	QString eta;
	if ( progress.processed >= progress.total )
		eta = "complete";
	else if ( progress.processed == 0 )
		eta = "estimating";
	else if ( !progress.remaining )
		eta = "waiting for progress";
	else
	{
		const auto seconds = progress.remaining->count();
		eta = QString( "%1:%2:%3" )
		          .arg( seconds / 3600, 2, 10, QChar( '0' ) )
		          .arg( ( seconds % 3600 ) / 60, 2, 10, QChar( '0' ) )
		          .arg( seconds % 60, 2, 10, QChar( '0' ) );
	}

	ui->statusLabel->setText(
		QString( "%1 ETA: %2\nRate: %3/s | Avg: %4/min" )
			.arg( phase_name )
			.arg( eta )
			.arg( idhan::hydrus::formatImportRate( progress.recent_per_second ) )
			.arg( idhan::hydrus::formatImportRate( progress.average_per_second * 60 ) ) );
}

void TagServiceWidget::updateProcessed()
{
	const auto to_process { m_info.num_mappings + m_info.num_parents + m_info.num_aliases };
	const auto total_processed { mappings_processed + parents_processed + aliases_processed };

	// Report progress as a 0-100 percentage so PTR-scale counts (which exceed INT_MAX) can't overflow the bar.
	ui->progressBar->setMaximum( 100 );
	ui->progressBar->setValue( to_process == 0 ? 0 : static_cast< int >( ( total_processed * 100 ) / to_process ) );
}

void TagServiceWidget::processedMappings( std::size_t count, std::size_t record_count )
{
	mappings_processed += count;
	records_processed += record_count;

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

void TagServiceWidget::preprocessingFinished( const bool success )
{
	m_preprocessed = success;
	updateProcessed();
	if ( success ) m_status->finish( "Ready to import" );
	emit preprocessingComplete( success );
}

void TagServiceWidget::importFinished()
{
	m_importing = false;
	m_time_timer.stop();
	m_status->finish( "Finished" );
	ui->statusLabel->setText(
		QString( "Import elapsed: %L1s" )
			.arg( std::chrono::duration_cast< std::chrono::seconds >( Clock::now() - m_start ).count() ) );
}

void TagServiceWidget::importFailed( const QString& message )
{
	m_importing = false;
	m_time_timer.stop();
	ui->statusLabel->clear();
	m_status->fail( message );
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
	if ( !ui->cbShouldImport->isChecked() )
	{
		m_status->finish( "Skipped" );
		return;
	}

	if ( !m_preprocessed ) return;

	m_start = Clock::now();
	m_importing = true;
	m_phase.reset();
	m_status->begin( "Queued for import" );
	m_time_timer.start();
	updateTime();
	QThreadPool::globalInstance()->start( m_worker );
}

void TagServiceWidget::startPreImport()
{
	ui->statusLabel->clear();
	m_status->begin( "Queued for preprocessing" );
	QThreadPool::globalInstance()->start( m_worker );
}
