#include "PTRFlattenWidget.hpp"

#include <QFileDialog>
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

	m_worker = std::make_unique< idhan::hydrus::ptr::PTRFlattenWorker >( source.toStdString(), output.toStdString() );

	connect( m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::progress, this, &PTRFlattenWidget::onProgress );
	connect(
		m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::subProgress, this, &PTRFlattenWidget::onSubProgress );
	connect( m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::finished, this, &PTRFlattenWidget::onFinished );

	QThreadPool::globalInstance()->start( m_worker.get() );
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

void PTRFlattenWidget::onFinished( const bool success, const QString& message )
{
	m_flattening = false;
	ui->flattenButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	ui->statusLabel->setText( message );
	ui->statusLabel->setStyleSheet( success ? "QLabel { color: green; }" : "QLabel { color: red; }" );

	if ( success ) emit outputDirectoryChanged( ui->outputPath->text() );
}
