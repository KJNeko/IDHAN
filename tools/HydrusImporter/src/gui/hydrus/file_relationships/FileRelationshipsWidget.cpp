#include "FileRelationshipsWidget.hpp"

#include <moc_FileRelationshipsWidget.cpp>

#include "FileRelationshipsWorker.hpp"
#include "ui_FileRelationshipsWidget.h"

FileRelationshipsWidget::FileRelationshipsWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent ) :
  QWidget( parent ),
  m_importer( importer ),
  m_worker( new FileRelationshipsWorker( this, importer ) ),
  ui( new Ui::FileRelationshipsWidget() )
{
	ui->setupUi( this );
	m_status = new idhan::hydrus::ImportServiceStatus( ui->statusLabel, ui->timingLabel, this );

	connect(
		m_worker,
		&FileRelationshipsWorker::processedMaxDuplicates,
		this,
		&FileRelationshipsWidget::processedMaxDuplicates );
	connect(
		m_worker,
		&FileRelationshipsWorker::processedMaxAlternatives,
		this,
		&FileRelationshipsWidget::processedMaxAlternatives );

	connect(
		m_worker, &FileRelationshipsWorker::processedDuplicates, this, &FileRelationshipsWidget::processedDuplicates );
	connect(
		m_worker,
		&FileRelationshipsWorker::processedAlternatives,
		this,
		&FileRelationshipsWidget::processedAlternatives );

	connect( m_worker, &FileRelationshipsWorker::statusMessage, this, &FileRelationshipsWidget::statusMessage );
	connect(
		m_worker,
		&FileRelationshipsWorker::errorOccurred,
		m_status,
		&idhan::hydrus::ImportServiceStatus::fail,
		Qt::QueuedConnection );
	connect(
		m_worker,
		&FileRelationshipsWorker::warningOccurred,
		m_status,
		&idhan::hydrus::ImportServiceStatus::warning,
		Qt::QueuedConnection );
	connect(
		m_worker,
		&FileRelationshipsWorker::preprocessingFinished,
		this,
		&FileRelationshipsWidget::preprocessingFinished );
	connect( m_worker, &FileRelationshipsWorker::finished, this, &FileRelationshipsWidget::importFinished );
}

void FileRelationshipsWidget::updateText()
{
	if ( alternatives_processed > 0 || duplicates_processed > 0 )
	{
		ui->alternativesCount->setText(
			QString( "Alternative files: %L1 (%L2 processed)" )
				.arg( alternatives_total )
				.arg( alternatives_processed ) );
		ui->duplicatesCount->setText(
			QString( "Duplicate pairs: %L1 (%L2 processed)" ).arg( duplicates_total ).arg( duplicates_processed ) );
	}
	else
	{
		ui->alternativesCount->setText( QString( "Alternative files: %L1" ).arg( alternatives_total ) );
		ui->duplicatesCount->setText( QString( "Duplicate pairs: %L1" ).arg( duplicates_total ) );
	}

	const auto total { alternatives_total + duplicates_total };
	const auto processed { alternatives_processed + duplicates_processed };

	// Report progress as a 0-100 percentage so large counts can't overflow the int-based bar.
	ui->progressBar->setMaximum( 100 );
	ui->progressBar->setValue( total == 0 ? 0 : static_cast< int >( ( processed * 100 ) / total ) );
}

void FileRelationshipsWidget::startImport()
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

void FileRelationshipsWidget::startPreImport()
{
	m_status->begin( "Queued for preprocessing" );
	QThreadPool::globalInstance()->start( m_worker );
}

void FileRelationshipsWidget::statusMessage( const QString& msg )
{
	m_status->setOperation( msg );
}

void FileRelationshipsWidget::preprocessingFinished( const bool success )
{
	m_preprocessed = success;
	if ( success ) m_status->finish( "Ready to import" );
	emit preprocessingComplete( success );
}

void FileRelationshipsWidget::importFinished()
{
	m_status->finish( "Finished" );
}

void FileRelationshipsWidget::processedDuplicates( const std::size_t count )
{
	if ( count > duplicates_processed ) m_status->recordProgress();
	duplicates_processed = count;
	updateText();
}

void FileRelationshipsWidget::processedMaxDuplicates( const std::size_t count )
{
	duplicates_total = count;
	updateText();
}

void FileRelationshipsWidget::processedAlternatives( const std::size_t count )
{
	if ( count > alternatives_processed ) m_status->recordProgress();
	alternatives_processed = count;
	updateText();
}

void FileRelationshipsWidget::processedMaxAlternatives( const std::size_t count )
{
	alternatives_total = count;
	updateText();
}
