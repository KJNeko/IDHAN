//
// Created by kj16609 on 6/28/25.
//
// You may need to build the project (run Qt uic code generator) to get "ui_HydrusImporter.h" resolved

#include "HydrusImporterWidget.hpp"

#include <moc_HydrusImporterWidget.cpp>

#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>

#include "HydrusImporter.hpp"
#include "file_relationships/FileRelationshipsWidget.hpp"
#include "tag_service/TagServiceWidget.hpp"
#include "ui_HydrusImporterWidget.h"
#include "urls/UrlServiceWidget.hpp"

class TagServiceWorker;

namespace
{

//! Result of opening a Hydrus database off the GUI thread. Holds a raw importer pointer (HydrusImporter is
//! non-movable, so it can't be returned by value) that the GUI thread adopts into a unique_ptr.
struct ParseResult
{
	idhan::hydrus::HydrusImporter* importer { nullptr };
	std::vector< idhan::hydrus::ServiceInfo > services {};
	bool has_ptr { false };
	QString error {};
};

} // namespace

HydrusImporterWidget::HydrusImporterWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::HydrusImporterWidget )
{
	ui->setupUi( this );

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

void HydrusImporterWidget::parseTagServices( const std::vector< idhan::hydrus::ServiceInfo >& services )
{
	for ( const auto& service : services )
	{
		if ( service.name == "public tag repository" && !ui->cbProcessPTR->isChecked() )
		{
			continue;
		}

		auto widget { new TagServiceWidget( m_importer.get(), this ) };

		widget->setName( service.name );
		widget->setInfo( service );

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
		connect(
			widget, &TagServiceWidget::preprocessingComplete, this, &HydrusImporterWidget::onPreprocessingComplete );
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
	++m_total_preprocess;
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
	connect(
		widget, &FileRelationshipsWidget::preprocessingComplete, this, &HydrusImporterWidget::onPreprocessingComplete );

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
	connect( widget, &UrlServiceWidget::preprocessingComplete, this, &HydrusImporterWidget::onPreprocessingComplete );

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
	ui->parseStatusLabel->setText( "Opening Hydrus database..." );
	ui->importButton->setEnabled( false );
	ui->parseHydrusDB->setEnabled( false );

	const std::filesystem::path path { ui->hydrusFolderPath->text().toStdString() };

	// Open the database and read its metadata off the GUI thread so the window stays responsive.
	// Widget construction has to happen back on the GUI thread, so it runs in the watcher below.
	auto future { QtConcurrent::run(
		[ path ]() -> ParseResult
		{
			ParseResult result {};
			try
			{
				auto importer { std::make_unique< idhan::hydrus::HydrusImporter >( path ) };
				result.services = importer->getTagServices();
				result.has_ptr = importer->hasPTR();
				result.importer = importer.release();
			}
			catch ( const std::exception& e )
			{
				result.error = QString::fromStdString( e.what() );
			}
			return result;
		} ) };

	auto* watcher { new QFutureWatcher< ParseResult >( this ) };
	connect(
		watcher,
		&QFutureWatcher< ParseResult >::finished,
		this,
		[ this, watcher ]()
		{
			watcher->deleteLater();
			ParseResult result { watcher->result() };

			if ( !result.error.isEmpty() )
			{
				ui->parseStatusLabel->setText( QString( "Failed to open Hydrus database: %1" ).arg( result.error ) );
				ui->parseHydrusDB->setEnabled( true );
				return;
			}

			m_importer.reset( result.importer );
			m_has_ptr = result.has_ptr;

			m_total_preprocess = 0;
			m_completed_preprocess = 0;

			parseTagServices( result.services );
			parseFileRelationships();
			parseUrls();

			// TODO: file-storage copy is intentionally not wired in yet. When implemented, invoke
			// m_importer->copyFileStorage() here so the Hydrus file clusters are registered with IDHAN.

			updatePreprocessProgress();

			emit triggerPreImport();
		} );
	watcher->setFuture( future );
}

void HydrusImporterWidget::onPreprocessingComplete()
{
	++m_completed_preprocess;
	updatePreprocessProgress();

	if ( m_completed_preprocess >= m_total_preprocess )
	{
		ui->importButton->setEnabled( true );
		ui->parseHydrusDB->setEnabled( true );
	}
}

void HydrusImporterWidget::updatePreprocessProgress()
{
	const bool has_ptr { m_has_ptr };

	if ( m_total_preprocess == 0 )
	{
		ui->parseStatusLabel->setText( QString( "Has PTR: %1 | Scanning..." ).arg( has_ptr ? "Yes" : "No" ) );
		return;
	}

	if ( m_completed_preprocess >= m_total_preprocess )
	{
		ui->parseStatusLabel->setText(
			QString( "Has PTR: %1 | Preprocessing complete" ).arg( has_ptr ? "Yes" : "No" ) );
	}
	else
	{
		ui->parseStatusLabel->setText(
			QString( "Has PTR: %1 | Preprocessing: %2/%3" )
				.arg( has_ptr ? "Yes" : "No" )
				.arg( m_completed_preprocess )
				.arg( m_total_preprocess ) );
	}
}

void HydrusImporterWidget::on_importButton_pressed()
{
	emit triggerImport();

	ui->importButton->setEnabled( false );
}
