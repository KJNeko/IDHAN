//
// Created by kj16609 on 6/28/25.
//
// You may need to build the project (run Qt uic code generator) to get "ui_HydrusImporter.h" resolved

#include "HydrusImporterWidget.hpp"

#include <moc_HydrusImporterWidget.cpp>

#include <QFileDialog>
#include <QFileInfo>

#include "HydrusImporter.hpp"
#include "file_relationships/FileRelationshipsWidget.hpp"
#include "tag_service/TagServiceWidget.hpp"
#include "ui_HydrusImporterWidget.h"
#include "urls/UrlServiceWidget.hpp"

class TagServiceWorker;

HydrusImporterWidget::HydrusImporterWidget( QWidget* parent ) :
  QWidget( parent ),
  m_threads(),
  ui( new Ui::HydrusImporterWidget )
{
	ui->setupUi( this );

	ui->cbProcessPTR->setChecked( true );

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

void HydrusImporterWidget::parseTagServices()
{
	auto service_infos { m_importer->getTagServices() };

	for ( const auto& service : service_infos )
	{
		if ( service.name == "public tag repository" && !ui->cbProcessPTR->isChecked() )
		{
			// idhan::logging::info( "Skipping PTR because cbProcessPTR is not checked" );
			continue;
		}

		auto widget { new TagServiceWidget( m_importer.get(), this ) };

		widget->setName( service.name );
		widget->setInfo( service );

		// ui->tagServicesLayout->addWidget( widget );
		addServiceWidget( widget );

		connect(
			this,
			&HydrusImporterWidget::triggerImport,
			widget,
			&TagServiceWidget::startImport,
			Qt::SingleShotConnection );
		connect(
			this,
			&HydrusImporterWidget::triggerPreImport,
			widget,
			&TagServiceWidget::startPreImport,
			Qt::SingleShotConnection );
	}
}

void HydrusImporterWidget::addServiceWidget( QWidget* widget )
{
	auto* groupFrame = new QFrame( this );
	groupFrame->setFrameShape( QFrame::Box );
	groupFrame->setFrameShadow( QFrame::Plain );
	groupFrame->setLineWidth( 1 );
	groupFrame->setStyleSheet( "QFrame { border: 1px solid #444; border-radius: 6px; }" );

	auto* groupLayout = new QVBoxLayout( groupFrame );
	groupLayout->setContentsMargins( 6, 6, 6, 6 );
	groupLayout->addWidget( widget );
	widget->setStyleSheet( "QFrame { border: none; }" );

	ui->tagServicesLayout->addWidget( groupFrame );
}

void HydrusImporterWidget::parseFileRelationships()
{
	auto* widget { new FileRelationshipsWidget( m_importer.get() ) };

	connect(
		this,
		&HydrusImporterWidget::triggerImport,
		widget,
		&FileRelationshipsWidget::startImport,
		Qt::SingleShotConnection );
	connect(
		this,
		&HydrusImporterWidget::triggerPreImport,
		widget,
		&FileRelationshipsWidget::startPreImport,
		Qt::SingleShotConnection );

	addServiceWidget( widget );
}

void HydrusImporterWidget::parseUrls()
{
	auto* widget { new UrlServiceWidget( m_importer.get() ) };

	connect(
		this, &HydrusImporterWidget::triggerImport, widget, &UrlServiceWidget::startImport, Qt::SingleShotConnection );
	connect(
		this,
		&HydrusImporterWidget::triggerPreImport,
		widget,
		&UrlServiceWidget::startPreImport,
		Qt::SingleShotConnection );

	addServiceWidget( widget );
}

void HydrusImporterWidget::on_hydrusFolderPath_textChanged( [[maybe_unused]] const QString& path )
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
	ui->parseStatusLabel->setText( QString( "Has PTR: %1" ).arg( has_ptr ? "Yes" : "No" ) );

	parseTagServices();
	parseFileRelationships();
	parseUrls();

	emit triggerPreImport();

	ui->importButton->setEnabled( true );
}

void HydrusImporterWidget::on_importButton_pressed()
{
	emit triggerImport();

	ui->importButton->setEnabled( false );
}
