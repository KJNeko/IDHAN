//
// Created by kj16609 on 11/5/25.
//
#include "FileRelationshipsWidget.hpp"

#include "FileRelationshipsWorker.hpp"
#include "ui_FileRelationshipsWidget.h"

FileRelationshipsWidget::FileRelationshipsWidget( idhan::hydrus::HydrusImporter* importer, QWidget* parent ) :
  QWidget( parent ),
  m_importer( importer ),
  m_worker( new FileRelationshipsWorker( this, importer ) ),
  ui( new Ui::FileRelationshipsWidget() )
{
	ui->setupUi( this );

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
}

void FileRelationshipsWidget::updateText()
{
	if ( alternatives_processed > 0 || duplicates_processed > 0 )
	{
		ui->alternativesCount->setText(
			QString( "Alternative groups: %L1 (%L2 processed)" )
				.arg( alternatives_total )
				.arg( alternatives_processed ) );
		ui->duplicatesCount->setText(
			QString( "Duplicate pairs: %L1 (%L2 processed)" ).arg( duplicates_total ).arg( duplicates_processed ) );
	}
	else
	{
		ui->alternativesCount->setText( QString( "Alternative groups: %L1" ).arg( alternatives_total ) );
		ui->duplicatesCount->setText( QString( "Duplicate pairs: %L1" ).arg( duplicates_total ) );
	}

	ui->progressBar->setMaximum( alternatives_total + duplicates_total );
	ui->progressBar->setValue( alternatives_processed + duplicates_processed );
}

void FileRelationshipsWidget::startImport()
{
	if ( ui->cbShouldImport->isChecked() ) QThreadPool::globalInstance()->start( m_worker );
}

void FileRelationshipsWidget::startPreImport()
{
	QThreadPool::globalInstance()->start( m_worker );
}

void FileRelationshipsWidget::statusMessage( const QString& msg )
{
	ui->statusLabel->setText( msg );
}

void FileRelationshipsWidget::processedDuplicates( const std::size_t count )
{
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
	alternatives_processed = count;
	updateText();
}

void FileRelationshipsWidget::processedMaxAlternatives( const std::size_t count )
{
	alternatives_total = count;
	updateText();
}