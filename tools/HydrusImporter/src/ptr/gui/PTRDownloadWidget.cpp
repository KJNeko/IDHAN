#include "PTRDownloadWidget.hpp"

#include <QFileDialog>
#include <QLocale>
#include <QStandardPaths>
#include <QThreadPool>

#include "ptr/PTRDownloader.hpp"
#include "ui_PTRDownloadWidget.h"

PTRDownloadWidget::PTRDownloadWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::PTRDownloadWidget )
{
	ui->setupUi( this );

	const QString default_dir = QStandardPaths::writableLocation( QStandardPaths::DownloadLocation ) + "/ptrfiles";
	ui->directoryPath->setText( default_dir );
	m_directory = default_dir.toStdString();
	ui->downloadButton->setEnabled( true );

	connect( ui->selectDirectory, &QToolButton::clicked, this, &PTRDownloadWidget::onSelectDirectory );
	connect(
		ui->directoryPath,
		&QLineEdit::textChanged,
		this,
		[ this ]( const QString& text )
		{
			ui->downloadButton->setEnabled( !text.isEmpty() );
			m_directory = text.toStdString();
			emit directoryChanged( text );
		} );
	connect( ui->downloadButton, &QPushButton::clicked, this, &PTRDownloadWidget::onDownload );
}

PTRDownloadWidget::~PTRDownloadWidget()
{
	delete ui;
}

void PTRDownloadWidget::onSelectDirectory()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this,
		"Select PTR download directory",
		ui->directoryPath->text(),
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks );

	if ( !dir.isEmpty() )
	{
		ui->directoryPath->setText( dir );
		m_directory = dir.toStdString();
	}
}

void PTRDownloadWidget::onDownload()
{
	if ( m_downloading ) return;

	m_downloading = true;
	ui->downloadButton->setEnabled( false );
	ui->progressBar->setValue( 0 );
	ui->statusLabel->setText( "Starting download..." );

	m_downloader = std::make_unique< idhan::hydrus::ptr::PTRDownloader >( m_directory );

	connect(
		m_downloader.get(),
		&idhan::hydrus::ptr::PTRDownloader::metadataReceived,
		this,
		&PTRDownloadWidget::onMetadataReceived );
	connect(
		m_downloader.get(),
		&idhan::hydrus::ptr::PTRDownloader::fileDownloading,
		this,
		&PTRDownloadWidget::onFileDownloading );
	connect(
		m_downloader.get(),
		&idhan::hydrus::ptr::PTRDownloader::fileDownloaded,
		this,
		&PTRDownloadWidget::onFileDownloaded );
	connect(
		m_downloader.get(),
		&idhan::hydrus::ptr::PTRDownloader::finished,
		this,
		&PTRDownloadWidget::onDownloadFinished );
	connect( m_downloader.get(), &idhan::hydrus::ptr::PTRDownloader::progress, this, &PTRDownloadWidget::onProgress );

	m_downloader->startSync();
}

void PTRDownloadWidget::onMetadataReceived( int total_updates, int total_files )
{
	ui->updateCountLabel->setText( QString( "Updates: %1" ).arg( QLocale::system().toString( total_updates ) ) );
	ui->fileCountLabel->setText( QString( "Files: %1" ).arg( QLocale::system().toString( total_files ) ) );
	ui->progressBar->setMaximum( total_files );
}

void PTRDownloadWidget::onFileDownloading( const QString& hash_hex, int current, int total )
{
	Q_UNUSED( hash_hex );
	ui->progressBar->setValue( current - 1 );
	ui->progressBar->setMaximum( total );
}

void PTRDownloadWidget::onFileDownloaded( const QString& hash_hex, int current, int total )
{
	Q_UNUSED( hash_hex );
	ui->progressBar->setValue( current );
	ui->progressBar->setMaximum( total );
	ui->fileCountLabel->setText( QString( "Files: %1/%2" )
	                                 .arg( QLocale::system().toString( current ) )
	                                 .arg( QLocale::system().toString( total ) ) );
}

void PTRDownloadWidget::onDownloadFinished( bool success, const QString& message )
{
	m_downloading = false;
	ui->downloadButton->setEnabled( true );

	if ( success )
	{
		ui->statusLabel->setText( message );
		ui->statusLabel->setStyleSheet( "QLabel { color: green; }" );
	}
	else
	{
		ui->statusLabel->setText( message );
		ui->statusLabel->setStyleSheet( "QLabel { color: red; }" );
	}
}

void PTRDownloadWidget::onProgress( const QString& status )
{
	ui->statusLabel->setText( status );
}
