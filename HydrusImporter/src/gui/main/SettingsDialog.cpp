//
// Created by kj16609 on 6/26/25.
//

#include "SettingsDialog.hpp"

#include <QFutureWatcher>

#include "idhan/IDHANClient.hpp"
#include "ui_SettingsDialog.h"

SettingsDialog::SettingsDialog( QWidget* parent ) : QDialog( parent ), ui( new Ui::SettingsDialog )
{
	ui->setupUi( this );

	ui->applySettings->setEnabled( false );
	ui->saveSettings->setEnabled( false );
}

SettingsDialog::~SettingsDialog()
{
	delete ui;
}

void SettingsDialog::on_testConnection_pressed()
{
	using namespace idhan;

	auto& client { IDHANClient::instance() };

	client.openConnection( ui->leHostname->text(), ui->lePort->text().toInt() );

	auto future { client.queryVersion() };

	auto* watcher { new QFutureWatcher< VersionInfo >() };
	watcher->setFuture( future );

	auto handleFuture = [ watcher, this ]()
	{
		try
		{
			const auto result { watcher->result() };

			ui->networkSettingsLabel->setText( QString( "Connected to IDHAN v%1 (Build: %4, Commit: %5)" )
			                                       .arg( result.server.str )
			                                       .arg( result.build_type )
			                                       .arg( result.commit ) );
		}
		catch ( std::exception& e )
		{
			ui->networkSettingsLabel->setText( QString( "Failed to connect to server: %1" ).arg( e.what() ) );
		}
		catch ( ... )
		{
			ui->networkSettingsLabel->setText( "Failed to connect to server" );
		}

		watcher->deleteLater();
	};

	connect( watcher, &QFutureWatcher< VersionInfo >::finished, handleFuture );
}

void SettingsDialog::loadSettings()
{
	ui->leHostname->setText( settings.value( "hostname", "localhost" ).toString() );
	ui->lePort->setText( settings.value( "port", "16609" ).toString() );
}

void SettingsDialog::on_saveSettings_pressed()
{}

void SettingsDialog::on_cancelSettings_pressed()
{
	this->close();
}

void SettingsDialog::on_applySettings_pressed()
{
	settings.setValue( "hostname", ui->leHostname->text() );
	settings.setValue( "port", ui->lePort->text().toInt() );
}

void SettingsDialog::wakeButtons()
{
	ui->applySettings->setEnabled( true );
	ui->saveSettings->setEnabled( true );
}
