// You may need to build the project (run Qt uic code generator) to get "ui_UrlServiceWidget.h" resolved

#include "UrlServiceWidget.hpp"

#include <QThreadPool>

#include "UrlServiceWorker.hpp"
#include "ui_UrlServiceWidget.h"

UrlServiceWidget::UrlServiceWidget( idhan::hydrus::HydrusImporter* get, QWidget* parent ) :
  QWidget( parent ),
  ui( new Ui::UrlServiceWidget() ),
  m_importer( get )
{
	ui->setupUi( this );
	m_status = new idhan::hydrus::ImportServiceStatus( ui->statusLabel, ui->timingLabel, this );

	m_worker = new UrlServiceWorker( this, m_importer );

	connect( m_worker, &UrlServiceWorker::processedMaxUrls, this, &UrlServiceWidget::processedMaxUrls );
	connect( m_worker, &UrlServiceWorker::processedUrls, this, &UrlServiceWidget::processedUrls );
	connect( m_worker, &UrlServiceWorker::statusMessage, this, &UrlServiceWidget::statusMessage );
	connect(
		m_worker,
		&UrlServiceWorker::errorOccurred,
		m_status,
		&idhan::hydrus::ImportServiceStatus::fail,
		Qt::QueuedConnection );
	connect( m_worker, &UrlServiceWorker::preprocessingFinished, this, &UrlServiceWidget::preprocessingFinished );
	connect( m_worker, &UrlServiceWorker::finished, this, &UrlServiceWidget::importFinished );
}

UrlServiceWidget::~UrlServiceWidget()
{
	delete ui;
}

void UrlServiceWidget::startPreImport()
{
	m_status->begin( "Queued for preprocessing" );
	QThreadPool::globalInstance()->start( m_worker );
}

void UrlServiceWidget::startImport()
{
	if ( !ui->cbShouldImport->isChecked() )
	{
		m_status->finish( "Skipped" );
		return;
	}

	if ( !m_preprocessed ) return;

	m_status->begin( "Queued for import" );
	QThreadPool::globalInstance()->start( m_worker );
}

void UrlServiceWidget::statusMessage( const QString& msg )
{
	m_status->setOperation( msg );
}

void UrlServiceWidget::preprocessingFinished( const bool success )
{
	m_preprocessed = success;
	if ( success ) m_status->finish( "Ready to import" );
	emit preprocessingComplete( success );
}

void UrlServiceWidget::importFinished()
{
	m_status->finish( "Finished" );
}

void UrlServiceWidget::processedMaxUrls( const std::size_t count, const std::size_t unique_count )
{
	ui->urlCount->setText( QString( "URL mappings: %L1\nUnique URLs: %L2" ).arg( count ).arg( unique_count ) );
	// Percentage-based so counts beyond INT_MAX can't overflow the bar.
	ui->progressBar->setMaximum( 100 );
	m_max_urls = count;
	m_max_unique_urls = unique_count;
}

void UrlServiceWidget::processedUrls( const std::size_t count, const std::size_t unique_count )
{
	m_status->recordProgress();
	ui->urlCount->setText(
		QString( "URL mappings: %L1 (%L2 processed)\nUnique URLs: %L3 (%L4 processed)" )
			.arg( m_max_urls )
			.arg( count )
			.arg( m_max_unique_urls )
			.arg( unique_count ) );
	ui->progressBar->setValue( m_max_urls == 0 ? 0 : static_cast< int >( ( count * 100 ) / m_max_urls ) );
}
