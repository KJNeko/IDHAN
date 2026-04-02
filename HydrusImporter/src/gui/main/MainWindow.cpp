//
// Created by kj16609 on 6/26/25.
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
#include "gui/hydrus/tag_service/TagServiceWidget.hpp"
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
