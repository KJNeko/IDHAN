#include "PTRImportWidget.hpp"

#include <QFileDialog>
#include <QHeaderView>
#include <QLocale>
#include <QStandardPaths>
#include <QThreadPool>

#include <spdlog/spdlog.h>

#include <filesystem>

#include "PTRHistoryModel.hpp"
#include "ptr/PTRImportWorker.hpp"
#include "ptr/flatten/Manifest.hpp"
#include "ui_PTRImportWidget.h"

PTRImportWidget::PTRImportWidget( QWidget* parent ) :
  QWidget( parent ),
  ui( new Ui::PTRImportWidget ),
  m_history_model( new PTRHistoryModel( this ) )
{
	ui->setupUi( this );

	const QString default_dir = QStandardPaths::writableLocation( QStandardPaths::DownloadLocation ) + "/ptrfiles";
	ui->directoryPath->setText( default_dir );
	ui->importButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	ui->historyView->setModel( m_history_model );
	ui->historyView->horizontalHeader()->setSectionResizeMode( QHeaderView::Interactive );
	ui->historyView->resizeColumnsToContents();

	connect( ui->selectDirectory, &QToolButton::clicked, this, &PTRImportWidget::onSelectDirectory );
	connect(
		ui->directoryPath,
		&QLineEdit::textChanged,
		this,
		[ this ]( const QString& text )
		{
			ui->importButton->setEnabled( !text.isEmpty() && !m_importing );
			updateCorpusNote();
		} );

	updateCorpusNote();
	connect( ui->importButton, &QPushButton::clicked, this, &PTRImportWidget::onImport );
	connect( ui->cancelButton, &QPushButton::clicked, this, &PTRImportWidget::onCancel );
}

PTRImportWidget::~PTRImportWidget()
{
	if ( m_worker )
	{
		m_worker->requestCancel();
		m_worker->disconnect();
		QThreadPool::globalInstance()->waitForDone();
	}
	delete ui;
}

void PTRImportWidget::setDirectory( const QString& path )
{
	ui->directoryPath->setText( path );
}

void PTRImportWidget::updateCorpusNote()
{
	const auto path = std::filesystem::path( ui->directoryPath->text().toStdString() );

	if ( !idhan::hydrus::ptr::isCompactedDirectory( path ) )
	{
		ui->corpusNoteLabel->setVisible( false );
		return;
	}

	bool discarded { false };
	try
	{
		discarded = idhan::hydrus::ptr::readManifest( path ).discard_terminal_deletes;
	}
	catch ( const std::exception& e )
	{
		spdlog::debug( "Could not read the manifest at {} for the corpus note: {}", path.string(), e.what() );
		ui->corpusNoteLabel->setVisible( false );
		return;
	}

	ui->corpusNoteLabel->setVisible( discarded );
	if ( discarded )
		ui->corpusNoteLabel->setText(
			"This corpus was flattened without tag removals, so importing it will only add tags." );
}

void PTRImportWidget::onSelectDirectory()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this,
		"Select PTR files directory",
		ui->directoryPath->text(),
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks );

	if ( !dir.isEmpty() ) ui->directoryPath->setText( dir );
}

void PTRImportWidget::onImport()
{
	const auto dir_text = ui->directoryPath->text();
	if ( m_importing || dir_text.isEmpty() ) return;

	m_importing = true;
	ui->importButton->setEnabled( false );
	ui->cancelButton->setEnabled( true );
	ui->progressBar->setValue( 0 );
	ui->subProgressBar->setValue( 0 );
	ui->statusLabel->setStyleSheet( "" );
	ui->statusLabel->setText( "Importing..." );
	ui->fileCountLabel->setText( "Files: --" );
	m_history_model->clear();

	m_worker = std::make_unique< idhan::hydrus::ptr::PTRImportWorker >( dir_text.toStdString() );

	connect( m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::progress, this, &PTRImportWidget::onProgress );
	connect( m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::subProgress, this, &PTRImportWidget::onSubProgress );
	connect(
		m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::fileProcessed, this, &PTRImportWidget::onFileProcessed );
	connect(
		m_worker.get(),
		&idhan::hydrus::ptr::PTRImportWorker::updateCompleted,
		this,
		&PTRImportWidget::onUpdateCompleted );
	connect( m_worker.get(), &idhan::hydrus::ptr::PTRImportWorker::finished, this, &PTRImportWidget::onImportFinished );

	QThreadPool::globalInstance()->start( m_worker.get() );
}

void PTRImportWidget::onCancel()
{
	if ( m_worker ) m_worker->requestCancel();
}

void PTRImportWidget::onProgress( const QString& status )
{
	ui->statusLabel->setText( status );
}

void PTRImportWidget::onSubProgress( int current, int total, const QString& status )
{
	ui->statusLabel->setText( status );
	ui->subProgressBar->setMaximum( total );
	ui->subProgressBar->setValue( current );
}

void PTRImportWidget::onFileProcessed( int current, int total )
{
	ui->progressBar->setMaximum( total );
	ui->progressBar->setValue( current );
	ui->fileCountLabel->setText( QString( "Files: %1 / %2" )
	                                 .arg( QLocale::system().toString( current ) )
	                                 .arg( QLocale::system().toString( total ) ) );
	ui->subProgressBar->setValue( 0 ); // Reset sub-progress for next file
}

void PTRImportWidget::onUpdateCompleted( const idhan::hydrus::ptr::PTRHistoryEntry& entry )
{
	m_history_model->addEntry( entry );
	ui->historyView->scrollToBottom();

	constexpr int COLUMN_RESIZE_INTERVAL = 20;
	if ( m_history_model->rowCount() % COLUMN_RESIZE_INTERVAL == 0 ) ui->historyView->resizeColumnsToContents();
}

void PTRImportWidget::onImportFinished( bool success, const QString& message )
{
	m_importing = false;
	ui->importButton->setEnabled( !ui->directoryPath->text().isEmpty() );
	ui->cancelButton->setEnabled( false );
	ui->subProgressBar->setValue( 0 );

	ui->statusLabel->setText( message );
	ui->statusLabel->setStyleSheet( success ? "QLabel { color: green; }" : "QLabel { color: red; }" );
}
