//
// Created by kj16609 on 6/28/25.
//
#include "TagServiceWidget.hpp"

#include <QWidget>

#include <iostream>

#include "ui_TagServiceWidget.h"

TagServiceWidget::TagServiceWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagServiceWidget )
{
	ui->setupUi( this );
}

TagServiceWidget::~TagServiceWidget()
{
	delete ui;
}

void TagServiceWidget::setName( const QString& name )
{
	m_name = name;
	ui->name->setText( QString( "Name: %1" ).arg( name ) );
}

void TagServiceWidget::processedMappings( std::size_t count )
{
	mappings_processed += count;
	QLocale locale { QLocale::English, QLocale::UnitedStates };
	locale.setNumberOptions( QLocale::DefaultNumberOptions );
	ui->mappingsCount->setText( QString( "Mappings: %1 (%2 processed)" )
	                                .arg( locale.toString( m_info.num_mappings ) )
	                                .arg( locale.toString( mappings_processed ) ) );
	ui->progressBar->setValue( mappings_processed + aliases_processed + parents_processed );
}

void TagServiceWidget::processedParents( std::size_t count )
{
	parents_processed += count;
	ui->parentsCount->setText( QString( "Parents: %1 (%2 processed)" )
	                               .arg( m_info.num_parents, 0, ',', ' ' )
	                               .arg( parents_processed, 0, ',', ' ' ) );
	ui->progressBar->setValue( mappings_processed + aliases_processed + parents_processed );
}

void TagServiceWidget::processedAliases( std::size_t count )
{
	aliases_processed += count;
	ui->aliasesCount->setText( QString( "Aliases: %1 (%2 processed)" )
	                               .arg( m_info.num_aliases, 0, ',', ' ' )
	                               .arg( aliases_processed, 0, ',', ' ' ) );
	ui->progressBar->setValue( mappings_processed + aliases_processed + parents_processed );
}

void TagServiceWidget::preprocessingFinished()
{
	m_preprocessed = true;
	this->ui->progressBar->setValue( 0 );
}

void TagServiceWidget::setMaxMappings( std::size_t count )
{
	m_info.num_mappings = count;
	ui->mappingsCount->setText( QString( "Mappings: %L1" ).arg( m_info.num_mappings ) );
}

void TagServiceWidget::setMaxParents( std::size_t count )
{
	m_info.num_parents = count;
	ui->parentsCount->setText( QString( "Parents: %1" ).arg( m_info.num_parents ) );
}

void TagServiceWidget::setMaxAliases( std::size_t count )
{
	m_info.num_aliases = count;
	ui->aliasesCount->setText( QString( "Aliases: %1" ).arg( m_info.num_aliases ) );
}

void TagServiceWidget::setInfo( const idhan::hydrus::ServiceInfo& service_info )
{
	ui->mappingsCount->setText( QString( "Mappings: %1" ).arg( service_info.num_mappings ) );
	ui->parentsCount->setText( QString( "Parents: %1" ).arg( service_info.num_parents ) );
	ui->aliasesCount->setText( QString( "Aliases: %1" ).arg( service_info.num_aliases ) );
	ui->progressBar->setMaximum( service_info.num_mappings + service_info.num_parents + service_info.num_aliases );
	m_info = service_info;
}