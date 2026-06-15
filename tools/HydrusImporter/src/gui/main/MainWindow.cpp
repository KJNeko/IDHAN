//
// Created by kj16609 on 5/2/25.
//
// You may need to build the project (run Qt uic code generator) to get "ui_MainWindow.h" resolved

#include "MainWindow.hpp"

#include <moc_MainWindow.cpp>

#include <QTimer>
#include <QUrl>

#include <idhan/IDHANClient.hpp>

#include "NET_CONSTANTS.hpp"
#include "SettingsDialog.hpp"
#include "gui/hydrus/HydrusImporterWidget.hpp"
#include "gui/hydrus/tag_management/TagManagementWidget.hpp"
#include "gui/hydrus/tag_service/TagServiceWidget.hpp"
#include "gui/recordtag/RecordTagWidget.hpp"
#include "ptr/gui/PTRDownloadWidget.hpp"
#include "ptr/gui/PTRImportWidget.hpp"
#include "ui_MainWindow.h"

MainWindow::MainWindow( QWidget* parent ) : QMainWindow( parent ), ui( new Ui::MainWindow )
{
	QString hostname;
	qint16 port;
	bool use_tls = false;

	if ( const auto env_url { qgetenv( "IDHAN_URL" ) }; !env_url.isEmpty() )
	{
		QUrl url { env_url };
		if ( url.scheme().isEmpty() )
		{
			// Try prepending a scheme to help QUrl parse host/port
			url = QUrl( "http://" + env_url );
		}

		hostname = url.host();
		port = static_cast< qint16 >( url.port( idhan::IDHAN_DEFAULT_PORT ) );
		use_tls = url.scheme() == "https";
	}
	else
	{
		hostname = settings.value( "hostname", "localhost" ).toString();
		port = static_cast< qint16 >(
			settings.value( "port", static_cast< uint >( idhan::IDHAN_DEFAULT_PORT ) ).toUInt() );
		use_tls = settings.value( "use_tls", false ).toBool();
	}

	m_client = std::make_unique< idhan::IDHANClient >(
		"Importer", hostname, port, settings.value( "key", "" ).toString(), use_tls );

	ui->setupUi( this );

	connect( ui->actionOptions, &QAction::triggered, this, &MainWindow::openSettings );
	connect( &heartbeat_timer, &QTimer::timeout, this, &MainWindow::checkHeartbeat );

#ifndef IMPORTER_TESTS
	if ( settings.value( "first_launch", true ).toBool() )
	{
		showSettings();
	}
	else
#endif
	{
		heartbeat_timer.start( 10000 );

		checkHeartbeat();
	}

#if IMPORTER_TESTS
	on_actionImport_Hydrus_triggered();
#endif

	ui->importTabs->addTab( new TagManagementWidget( this ), "Tag Management" );
	ui->importTabs->setCurrentIndex( ui->importTabs->count() - 1 );

	// Record Tag Editor tab
	m_recordTagWidget = new RecordTagWidget( this );
	m_recordTagTabIndex = ui->importTabs->addTab( m_recordTagWidget, "Record Tag Editor" );
	connect( m_recordTagWidget, &RecordTagWidget::detachRequested, this, &MainWindow::onDetachRecordTag );

	// PTR Importer tab (sub-tabs: Download + Import)
	connect( ui->actionImport_PTR, &QAction::triggered, this, &MainWindow::on_actionImport_PTR_triggered );
	on_actionImport_PTR_triggered();
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::showSettings()
{
	ui->actionOptions->trigger();
}

void MainWindow::openSettings()
{
	auto dialog { new SettingsDialog( nullptr ) };
	dialog->setWindowModality( Qt::ApplicationModal );
	this->setDisabled( true );
	dialog->show();
	dialog->setEnabled( true );
	connect(
		dialog,
		&SettingsDialog::finished,
		this,
		[ this, dialog ]()
		{
			this->setDisabled( false );
			dialog->deleteLater();

			heartbeat_timer.start( 10000 );

			checkHeartbeat();
		} );
}

void MainWindow::checkHeartbeat()
{
	using namespace idhan;

	auto& client { IDHANClient::instance() };

	auto future { client.queryVersion() };

	auto* watcher { new QFutureWatcher< VersionInfo >() };
	watcher->setFuture( future );

	auto handleFuture = [ watcher, this ]()
	{
		try
		{
			const auto result { watcher->result() };

			ui->statusbar->showMessage(
				QString( "Connected to IDHAN v%1 (Build: %4, Commit: %5)" )
					.arg( result.server.str )
					.arg( result.build_type )
					.arg( result.commit ) );
		}
		catch ( std::exception& e )
		{
			ui->statusbar->showMessage( QString( "Failed to connect to server: %1" ).arg( e.what() ) );
		}
		catch ( ... )
		{
			ui->statusbar->showMessage( "Failed to connect to server" );
		}

		watcher->deleteLater();
	};

	connect( watcher, &QFutureWatcher< VersionInfo >::finished, handleFuture );
}

void MainWindow::on_actionImport_File_triggered()
{}

void MainWindow::on_actionImport_Hydrus_triggered()
{
	ui->importTabs->addTab( new HydrusImporterWidget( this ), "Hydrus Importer" );
}

void MainWindow::on_actionImport_PTR_triggered()
{
	auto* ptr_tabs = new QTabWidget( this );
	auto* download_widget = new PTRDownloadWidget( ptr_tabs );
	auto* import_widget = new PTRImportWidget( ptr_tabs );
	ptr_tabs->addTab( download_widget, "Download" );
	ptr_tabs->addTab( import_widget, "Import" );

	connect( download_widget, &PTRDownloadWidget::directoryChanged, import_widget, &PTRImportWidget::setDirectory );

	ui->importTabs->addTab( ptr_tabs, "PTR Importer" );
	ui->importTabs->setCurrentIndex( ui->importTabs->count() - 1 );
}

void MainWindow::onDetachRecordTag()
{
	if ( m_recordTagDialog != nullptr ) return;

	// Remove widget from tab
	ui->importTabs->removeTab( m_recordTagTabIndex );

	// Create dialog and add widget
	m_recordTagDialog = new QDialog( nullptr, Qt::Window );
	m_recordTagDialog->setWindowTitle( "Record Tag Editor" );
	m_recordTagDialog->resize( 800, 600 );

	m_recordTagWidget->setParent( m_recordTagDialog );
	auto* layout = new QVBoxLayout( m_recordTagDialog );
	layout->setContentsMargins( 0, 0, 0, 0 );
	layout->addWidget( m_recordTagWidget );
	m_recordTagDialog->setLayout( layout );

	connect( m_recordTagDialog, &QDialog::finished, this, &MainWindow::onReattachRecordTag );

	m_recordTagDialog->show();
}

void MainWindow::onReattachRecordTag( [[maybe_unused]] int result )
{
	if ( m_recordTagDialog == nullptr ) return;

	// Remove widget from dialog
	m_recordTagWidget->setParent( this );
	m_recordTagDialog->hide();
	m_recordTagDialog->deleteLater();
	m_recordTagDialog = nullptr;

	// Add widget back to tab
	m_recordTagTabIndex = ui->importTabs->addTab( m_recordTagWidget, "Record Tag Editor" );
	ui->importTabs->setCurrentIndex( m_recordTagTabIndex );
}
