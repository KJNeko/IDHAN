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

void TagServiceWidget::updateTime()
{
	const std::size_t total_processed { ui->progressBar->value() };
	const std::size_t to_process { m_info.num_mappings + m_info.num_parents + m_info.num_aliases };

	if ( total_processed == -1 ) return;

	if ( to_process == total_processed )
	{
		ui->statusLabel->setText( "ETA: Finished" );
		return;
	}

	const auto time_elapsed {
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::high_resolution_clock::now() - m_start )
			.count()
	};

	const double ms_per_mapping { static_cast< double >( time_elapsed ) / static_cast< double >( total_processed ) };

	const auto time_remaining { static_cast< std::size_t >( ( to_process - total_processed ) * ms_per_mapping ) };

	const auto hours = time_remaining / 3600000;
	const auto minutes = ( time_remaining % 3600000 ) / 60000;
	const auto seconds = ( time_remaining % 60000 ) / 1000;

	ui->statusLabel->setText( QString( "ETA: %1:%2:%3 (%L4/%L5)" )
	                              .arg( hours, 2, 10, QChar( '0' ) )
	                              .arg( minutes, 2, 10, QChar( '0' ) )
	                              .arg( seconds, 2, 10, QChar( '0' ) )
	                              .arg( total_processed, 2, 10, QChar( '0' ) )
	                              .arg( to_process, 2, 10, QChar( '0' ) )

	);
}

void TagServiceWidget::processedMappings( std::size_t count )
{
	mappings_processed += count;
	QLocale locale { QLocale::English, QLocale::UnitedStates };
	locale.setNumberOptions( QLocale::DefaultNumberOptions );
	ui->mappingsCount
		->setText( QString( "Mappings: %L1 (%L2 processed)" ).arg( m_info.num_mappings ).arg( mappings_processed ) );
	ui->progressBar
		->setValue( static_cast< std::size_t >( mappings_processed + aliases_processed + parents_processed ) );
	ui->progressBar
		->setMaximum( static_cast< std::size_t >( m_info.num_mappings + m_info.num_parents + m_info.num_aliases ) );
	updateTime();
}

void TagServiceWidget::processedParents( std::size_t count )
{
	parents_processed += count;
	ui->parentsCount
		->setText( QString( "Parents: %L1 (%L2 processed)" ).arg( m_info.num_parents ).arg( parents_processed ) );
	ui->progressBar
		->setValue( static_cast< std::size_t >( mappings_processed + aliases_processed + parents_processed ) );
	ui->progressBar
		->setMaximum( static_cast< std::size_t >( m_info.num_mappings + m_info.num_parents + m_info.num_aliases ) );
	updateTime();
}

void TagServiceWidget::processedAliases( std::size_t count )
{
	aliases_processed += count;
	ui->aliasesCount
		->setText( QString( "Aliases: %L1 (%L2 processed)" ).arg( m_info.num_aliases ).arg( aliases_processed ) );
	ui->progressBar
		->setValue( static_cast< std::size_t >( mappings_processed + aliases_processed + parents_processed ) );
	ui->progressBar
		->setMaximum( static_cast< std::size_t >( m_info.num_mappings + m_info.num_parents + m_info.num_aliases ) );
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
		ui->mappingsCount->setText( QString( "Mappings: %L1 (%L2 processed)" )
		                                .arg( m_info.num_mappings )
		                                .arg( mappings_processed ) );
	else
		ui->mappingsCount->setText( QString( "Mappings: %L1" ).arg( m_info.num_mappings ) );
}

void TagServiceWidget::setMaxParents( std::size_t count )
{
	m_info.num_parents = count;
	if ( parents_processed > 0 )
		ui->parentsCount
			->setText( QString( "Parents: %L1 (%L2 processed)" ).arg( m_info.num_parents ).arg( parents_processed ) );
	else
		ui->parentsCount->setText( QString( "Parents: %L1" ).arg( m_info.num_parents ) );
}

void TagServiceWidget::setMaxAliases( std::size_t count )
{
	m_info.num_aliases = count;
	if ( aliases_processed > 0 )
		ui->aliasesCount
			->setText( QString( "Aliases: %L1 (%L2 processed)" ).arg( m_info.num_aliases ).arg( aliases_processed ) );
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