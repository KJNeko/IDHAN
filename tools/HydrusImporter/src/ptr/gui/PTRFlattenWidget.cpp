#include "PTRFlattenWidget.hpp"

#include <QFileDialog>
#include <QLocale>
#include <QStandardPaths>
#include <QThreadPool>

#include <filesystem>

#include "ptr/PTRFlattenWorker.hpp"
#include "ptr/flatten/RunFlatten.hpp"
#include "ui_PTRFlattenWidget.h"

PTRFlattenWidget::PTRFlattenWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::PTRFlattenWidget )
{
	ui->setupUi( this );

	const QString downloads = QStandardPaths::writableLocation( QStandardPaths::DownloadLocation );
	setDirectory( downloads + "/ptrfiles" );

	ui->flattenButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	connect( ui->selectSource, &QToolButton::clicked, this, &PTRFlattenWidget::onSelectSource );
	connect( ui->selectOutput, &QToolButton::clicked, this, &PTRFlattenWidget::onSelectOutput );
	connect( ui->flattenButton, &QPushButton::clicked, this, &PTRFlattenWidget::onFlatten );
	connect( ui->cancelButton, &QPushButton::clicked, this, &PTRFlattenWidget::onCancel );

	// The output follows the source until the user picks one themselves. textEdited rather than
	// textChanged, so the programmatic updates below do not read as a manual override.
	connect( ui->sourcePath, &QLineEdit::textEdited, this, &PTRFlattenWidget::onSourceEdited );
	connect( ui->outputPath, &QLineEdit::textEdited, this, [ this ] { m_output_overridden = true; } );
}

//! The compacted output lives in a subdirectory of the corpus, so the two travel together.
QString PTRFlattenWidget::defaultOutputFor( const QString& source )
{
	if ( source.isEmpty() ) return {};
	return QString::fromStdString(
		( std::filesystem::path( source.toStdString() ) / idhan::hydrus::ptr::COMPACT_SUBDIRECTORY ).string() );
}

PTRFlattenWidget::~PTRFlattenWidget()
{
	if ( m_worker )
	{
		m_worker->requestCancel();
		m_worker->disconnect();
		QThreadPool::globalInstance()->waitForDone();
	}
	delete ui;
}

void PTRFlattenWidget::setDirectory( const QString& path )
{
	ui->sourcePath->setText( path );
	if ( !m_output_overridden ) ui->outputPath->setText( defaultOutputFor( path ) );
}

void PTRFlattenWidget::onSourceEdited( const QString& path )
{
	if ( !m_output_overridden ) ui->outputPath->setText( defaultOutputFor( path ) );
}

void PTRFlattenWidget::onSelectSource()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, "Select PTR files directory", ui->sourcePath->text(), QFileDialog::ShowDirsOnly );
	if ( !dir.isEmpty() ) setDirectory( dir );
}

void PTRFlattenWidget::onSelectOutput()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, "Select output directory", ui->outputPath->text(), QFileDialog::ShowDirsOnly );
	if ( dir.isEmpty() ) return;

	ui->outputPath->setText( dir );
	m_output_overridden = true;
}

void PTRFlattenWidget::onFlatten()
{
	const auto source = ui->sourcePath->text();
	const auto output = ui->outputPath->text();
	if ( m_flattening || source.isEmpty() || output.isEmpty() ) return;

	m_flattening = true;
	ui->flattenButton->setEnabled( false );
	ui->cancelButton->setEnabled( true );
	ui->progressBar->setValue( 0 );
	ui->statusLabel->setStyleSheet( "" );
	ui->statusLabel->setText( "Starting..." );
	resetStats();

	// Read once, here: the run keeps whatever the checkbox said when it started, so toggling it
	// mid-flatten cannot change what the chunks end up containing.
	m_worker = std::make_unique< idhan::hydrus::ptr::PTRFlattenWorker >(
		source.toStdString(), output.toStdString(), ui->discardTerminalDeletes->isChecked() );

	connect( m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::progress, this, &PTRFlattenWidget::onProgress );
	connect(
		m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::subProgress, this, &PTRFlattenWidget::onSubProgress );
	connect(
		m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::statsUpdated, this, &PTRFlattenWidget::onStatsUpdated );
	connect( m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::finished, this, &PTRFlattenWidget::onFinished );

	QThreadPool::globalInstance()->start( m_worker.get() );
}

void PTRFlattenWidget::resetStats()
{
	ui->eventsScannedValue->setText( "0" );
	ui->recordsFlattenedValue->setText( "0" );
	ui->chainsCollapsedValue->setText( "0" );
	ui->terminalDeletesValue->setText( "0" );
	ui->terminalDeleteRecordsValue->setText( "0" );
	ui->chunksWrittenValue->setText( "0" );
	ui->skippedFilesValue->setText( "0" );
	ui->skippedMissingDefinitionsValue->setText( "0" );

	// Not a count yet. The tag totals cannot be known until the relations file is written, so a
	// zero here would read as "none were unused" for the whole run.
	ui->unusedTagsValue->setText( "-" );
}

void PTRFlattenWidget::onCancel()
{
	if ( m_worker ) m_worker->requestCancel();
}

void PTRFlattenWidget::onProgress( const QString& status )
{
	ui->statusLabel->setText( status );
}

void PTRFlattenWidget::onSubProgress( const int current, const int total, const QString& status )
{
	ui->statusLabel->setText( status );
	ui->progressBar->setMaximum( total );
	ui->progressBar->setValue( current );
}

void PTRFlattenWidget::onStatsUpdated( const idhan::hydrus::ptr::FlattenLiveStats& stats )
{
	const auto& locale = QLocale::system();
	const auto number = [ &locale ]( const std::uint64_t value )
	{ return locale.toString( static_cast< qulonglong >( value ) ); };

	ui->eventsScannedValue->setText( number( stats.events_scanned ) );
	ui->recordsFlattenedValue->setText( number( stats.records_flattened ) );
	ui->chainsCollapsedValue->setText( number( stats.chains_collapsed ) );
	ui->terminalDeletesValue->setText( number( stats.terminal_deletes ) );
	ui->terminalDeleteRecordsValue->setText( number( stats.terminal_delete_records ) );
	ui->chunksWrittenValue->setText( number( stats.chunks_written ) );
	ui->skippedFilesValue->setText( number( stats.skipped_files ) );
	ui->skippedMissingDefinitionsValue->setText( number( stats.skipped_missing_definitions ) );

	// The same number means opposite things depending on the checkbox, so the label has to say
	// which. Driven from the run's own stats rather than the checkbox, which the user is free to
	// toggle while a flatten is in flight.
	ui->terminalDeletesLabel->setText(
		stats.discard_terminal_deletes ? "Terminal deletes discarded:" : "Terminal deletes kept:" );

	if ( stats.tags_counted )
		ui->unusedTagsValue->setText( QString( "%1 of %2 defined" )
		                                  .arg( number( stats.defined_tags - stats.used_tags ) )
		                                  .arg( number( stats.defined_tags ) ) );
}

void PTRFlattenWidget::onFinished( const bool success, const QString& message )
{
	m_flattening = false;
	ui->flattenButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	ui->statusLabel->setText( message );
	ui->statusLabel->setStyleSheet( success ? "QLabel { color: green; }" : "QLabel { color: red; }" );

	if ( success ) emit outputDirectoryChanged( ui->outputPath->text() );
}
