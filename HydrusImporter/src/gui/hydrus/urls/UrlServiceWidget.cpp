//
// Created by kj16609 on 11/7/25.
//
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

	m_worker = new UrlServiceWorker( this, m_importer );

	connect( m_worker, &UrlServiceWorker::processedMaxUrls, this, &UrlServiceWidget::processedMaxUrls );
	connect( m_worker, &UrlServiceWorker::processedUrls, this, &UrlServiceWidget::processedUrls );
	connect( m_worker, &UrlServiceWorker::statusMessage, this, &UrlServiceWidget::statusMessage );
}

UrlServiceWidget::~UrlServiceWidget()
{
	delete ui;
}

void UrlServiceWidget::startPreImport()
{
	QThreadPool::globalInstance()->start( m_worker );
}

void UrlServiceWidget::startImport()
{
	if ( ui->cbShouldImport->isChecked() ) QThreadPool::globalInstance()->start( m_worker );
}

void UrlServiceWidget::statusMessage( const QString& msg )
{
	ui->statusLabel->setText( msg );
}

void UrlServiceWidget::processedMaxUrls( const std::size_t count )
{
	ui->urlCount->setText( QString( "URL mappingss: %L1" ).arg( count ) );
	ui->progressBar->setMaximum( static_cast< int >( count ) );
	m_max_urls = count;
}

void UrlServiceWidget::processedUrls( const std::size_t count )
{
	ui->urlCount->setText( QString( "URL mappingss: %L1 (%L2 processed)" ).arg( m_max_urls ).arg( count ) );
	ui->progressBar->setValue( static_cast< int >( count ) );
}