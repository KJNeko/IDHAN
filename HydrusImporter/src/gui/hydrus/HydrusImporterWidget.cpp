//
// Created by kj16609 on 6/28/25.
//
// You may need to build the project (run Qt uic code generator) to get "ui_HydrusImporter.h" resolved

#include "HydrusImporterWidget.hpp"

#include <QFileDialog>
#include <QFileInfo>

#include <ui_TagServiceWidget.h>

#include "HydrusImporter.hpp"
#include "TagServiceWidget.hpp"
#include "TagServiceWorker.hpp"
#include "ui_HydrusImporterWidget.h"

class TagServiceWorker;

HydrusImporterWidget::HydrusImporterWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::HydrusImporterWidget )
{
	ui->setupUi( this );

	// Process PTR by default?
	ui->cbProcessPTR->setChecked( false );

	ui->hyFolderStatusLabel->setText( "Invalid" );
	ui->hyFolderStatusLabel->setStyleSheet( "QLabel { color: red; }" );

#ifdef IMPORTER_TESTS
	ui->hydrusFolderPath->setText( "/home/kj16609/.local/share/hydrus/db/" );

	on_parseHydrusDB_pressed();
#endif
}

HydrusImporterWidget::~HydrusImporterWidget()
{
	delete ui;
}

void HydrusImporterWidget::on_hydrusFolderPath_textChanged( const QString& path )
{
	testHydrusPath();
}

void HydrusImporterWidget::testHydrusPath()
{
	const auto path_string { ui->hydrusFolderPath->text() };

	const QFileInfo fileInfo( path_string );
	bool exists = fileInfo.exists() && fileInfo.isDir();

	std::vector< QString > expected_files { "client.db", "client.mappings.db", "client.master.db" };

	if ( exists )
	{
		for ( const auto& file : expected_files )
		{
			const QFileInfo dbFile( path_string + "/" + file );
			if ( !dbFile.exists() || !dbFile.isFile() )
			{
				exists = false;
				break;
			}
		}
	}

	ui->hyFolderStatusLabel->setText( exists ? "Valid" : "Invalid" );
	ui->hyFolderStatusLabel->setStyleSheet( QString( "QLabel { color: %1; }" ).arg( exists ? "green" : "red" ) );

	ui->parseHydrusDB->setEnabled( exists );
}

void HydrusImporterWidget::on_selectHydrusPath_clicked()
{
	QString dir = QFileDialog::getExistingDirectory(
		this,
		tr( "Select Hydrus Directory" ),
		ui->hydrusFolderPath->text(),
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks );

	if ( !dir.isEmpty() )
	{
		ui->hydrusFolderPath->setText( dir );
		testHydrusPath();
	}
}

void HydrusImporterWidget::on_parseHydrusDB_pressed()
{
	ui->parseStatusLabel->setText( "Processing... This might take awhile. (Up to 2 minutes if you have the PTR)" );

	if ( QThread::isMainThread() )
	{
		QApplication::processEvents();
	}

	m_importer = std::make_unique< idhan::hydrus::HydrusImporter >( ui->hydrusFolderPath->text().toStdString() );

	const bool has_ptr { m_importer->hasPTR() };

	auto service_infos { m_importer->getTagServices() };

	ui->parseStatusLabel->setText( QString( "Has PTR: %1" ).arg( has_ptr ? "Yes" : "No" ) );

	for ( const auto& service : service_infos )
	{
		if ( service.name == "public tag repository" && !ui->cbProcessPTR->isChecked() )
		{
			// idhan::logging::info( "Skipping PTR because cbProcessPTR is not checked" );
			continue;
		}

		TagServiceWidget* widget { new TagServiceWidget( this ) };

		widget->setName( service.name );
		widget->setInfo( service );

		ui->tagServicesLayout->addWidget( widget );

		auto* worker { new TagServiceWorker( this, m_importer.get() ) };

		worker->setService( service );

		connect(
			worker,
			&TagServiceWorker::processedMappings,
			widget,
			&TagServiceWidget::processedMappings,
			Qt::AutoConnection );
		connect(
			worker,
			&TagServiceWorker::processedParents,
			widget,
			&TagServiceWidget::processedParents,
			Qt::AutoConnection );
		connect(
			worker,
			&TagServiceWorker::processedAliases,
			widget,
			&TagServiceWidget::processedAliases,
			Qt::AutoConnection );
		connect(
			worker,
			&TagServiceWorker::processedMaxMappings,
			widget,
			&TagServiceWidget::setMaxMappings,
			Qt::AutoConnection );
		connect(
			worker,
			&TagServiceWorker::processedMaxParents,
			widget,
			&TagServiceWidget::setMaxParents,
			Qt::AutoConnection );
		connect(
			worker,
			&TagServiceWorker::processedMaxAliases,
			widget,
			&TagServiceWidget::setMaxAliases,
			Qt::AutoConnection );
		connect(
			worker, &TagServiceWorker::finished, widget, &TagServiceWidget::preprocessingFinished, Qt::AutoConnection );

		m_workers.emplace_back( worker );

		if ( service.name == "public tag repository" ) worker->m_ptr = true;

		m_threads.start( worker );
	}

	ui->importButton->setEnabled( true );
}

void HydrusImporterWidget::on_importButton_pressed()
{
	for ( auto& worker : m_workers )
	{
		m_threads.start( worker );
	}

	ui->importButton->setEnabled( false );
}
