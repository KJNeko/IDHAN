#include "PTRImportWidget.hpp"

#include <QFileDialog>
#include <QThreadPool>

#include <fstream>

#include "ptr/PTRFileParser.hpp"
#include "ptr/PTRImportWorker.hpp"
#include "ui_PTRImportWidget.h"

PTRImportWidget::PTRImportWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::PTRImportWidget )
{
	ui->setupUi( this );

	ui->directoryPath->setText( "./ptrfiles" );
	m_directory = "./ptrfiles";
	ui->scanButton->setEnabled( true );

	connect( ui->selectDirectory, &QToolButton::clicked, this, &PTRImportWidget::onSelectDirectory );
	connect(
		ui->directoryPath,
		&QLineEdit::textChanged,
		this,
		[ this ]() { ui->scanButton->setEnabled( !ui->directoryPath->text().isEmpty() ); } );
	connect( ui->scanButton, &QPushButton::clicked, this, &PTRImportWidget::onScan );
	connect( ui->importButton, &QPushButton::clicked, this, &PTRImportWidget::onImport );
}

PTRImportWidget::~PTRImportWidget()
{
	delete ui;
}

void PTRImportWidget::setDirectory( const QString& path )
{
	ui->directoryPath->setText( path );
	m_directory = path.toStdString();
}

void PTRImportWidget::onSelectDirectory()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this,
		"Select PTR files directory",
		ui->directoryPath->text(),
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks );

	if ( !dir.isEmpty() )
	{
		ui->directoryPath->setText( dir );
		m_directory = dir.toStdString();
	}
}

void PTRImportWidget::onScan()
{
	if ( m_directory.empty() ) return;

	ui->statusLabel->setText( "Scanning directory..." );

	int def_count = 0;
	int content_count = 0;

	for ( const auto& entry : std::filesystem::directory_iterator( m_directory ) )
	{
		if ( !entry.is_regular_file() ) continue;
		if ( entry.path().extension() != ".ptrupdate" ) continue;

		try
		{
			const auto data = idhan::hydrus::ptr::readFile( entry.path() );
			const auto root = idhan::hydrus::ptr::decompressToJson( data );
			const auto type = idhan::hydrus::ptr::detectUpdateType( root );

			if ( type == idhan::hydrus::ptr::UpdateType::Definitions )
				++def_count;
			else if ( type == idhan::hydrus::ptr::UpdateType::Content )
				++content_count;

			m_total_files = def_count + content_count;
			ui->fileCountLabel->setText( QString( "PTR files: %1" ).arg( m_total_files ) );
			ui->mappingCountLabel->setText(
				QString( "Definitions: %1 | Content: %2" ).arg( def_count ).arg( content_count ) );

			QApplication::processEvents();
		}
		catch ( ... )
		{
			// Skip unparseable
		}
	}

	m_total_files = def_count + content_count;
	ui->fileCountLabel->setText( QString( "PTR files: %1" ).arg( m_total_files ) );
	ui->mappingCountLabel->setText( QString( "Definitions: %1 | Content: %2" ).arg( def_count ).arg( content_count ) );
	ui->statusLabel->setText(
		QString( "Found %1 PTR files (%2 definitions, %3 content)" )
			.arg( m_total_files )
			.arg( def_count )
			.arg( content_count ) );
	ui->importButton->setEnabled( content_count > 0 );
}

void PTRImportWidget::onImport()
{
	if ( m_importing || m_directory.empty() ) return;

	m_importing = true;
	ui->importButton->setEnabled( false );
	ui->scanButton->setEnabled( false );
	ui->progressBar->setValue( 0 );
	ui->statusLabel->setText( "Importing..." );

	m_worker = std::make_unique< idhan::hydrus::ptr::PTRImportWorker >( m_directory );

	connect( m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::progress, this, &PTRImportWidget::onProgress );
	connect(
		m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::fileProcessed, this, &PTRImportWidget::onFileProcessed );
	connect( m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::finished, this, &PTRImportWidget::onImportFinished );

	QThreadPool::globalInstance()->start( m_worker.get() );
}

void PTRImportWidget::onProgress( const QString& status )
{
	ui->statusLabel->setText( status );
}

void PTRImportWidget::onFileProcessed( const QString& hash_hex, int current, int total )
{
	Q_UNUSED( hash_hex );
	m_processed_files = current;
	ui->progressBar->setMaximum( total );
	ui->progressBar->setValue( current );
}

void PTRImportWidget::onImportFinished( bool success, const QString& message )
{
	m_importing = false;
	ui->importButton->setEnabled( true );
	ui->scanButton->setEnabled( true );

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
