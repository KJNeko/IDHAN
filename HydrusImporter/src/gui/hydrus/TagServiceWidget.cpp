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

void TagServiceWidget::processedMappings( std::size_t count )
{
	mappings_processed += count;
	QLocale locale { QLocale::English, QLocale::UnitedStates };
	locale.setNumberOptions( QLocale::DefaultNumberOptions );
	ui->mappingsCount
		->setText( QString( "Mappings: %L1 (%L2 processed)" ).arg( m_info.num_mappings ).arg( mappings_processed ) );
	ui->progressBar->setValue( static_cast< int >( mappings_processed + aliases_processed + parents_processed ) );
	this->ui->progressBar
		->setMaximum( static_cast< int >( m_info.num_mappings + m_info.num_parents + m_info.num_aliases ) );
}

void TagServiceWidget::processedParents( std::size_t count )
{
	parents_processed += count;
	ui->parentsCount
		->setText( QString( "Parents: %L1 (%L2 processed)" ).arg( m_info.num_parents ).arg( parents_processed ) );
	ui->progressBar->setValue( static_cast< int >( mappings_processed + aliases_processed + parents_processed ) );
	this->ui->progressBar
		->setMaximum( static_cast< int >( m_info.num_mappings + m_info.num_parents + m_info.num_aliases ) );
}

void TagServiceWidget::processedAliases( std::size_t count )
{
	aliases_processed += count;
	ui->aliasesCount
		->setText( QString( "Aliases: %L1 (%L2 processed)" ).arg( m_info.num_aliases ).arg( aliases_processed ) );
	ui->progressBar->setValue( static_cast< int >( mappings_processed + aliases_processed + parents_processed ) );
	this->ui->progressBar
		->setMaximum( static_cast< int >( m_info.num_mappings + m_info.num_parents + m_info.num_aliases ) );
}

void TagServiceWidget::preprocessingFinished()
{
	m_preprocessed = true;
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
}

void TagServiceWidget::startPreImport()
{
	QThreadPool::globalInstance()->start( m_worker );
}