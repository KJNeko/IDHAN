#include "PTRFlattenWidget.hpp"

#include <QFileDialog>
#include <QStandardPaths>
#include <QThreadPool>

#include "ptr/PTRFlattenWorker.hpp"
#include "ui_PTRFlattenWidget.h"

PTRFlattenWidget::PTRFlattenWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::PTRFlattenWidget )
{
	ui->setupUi( this );

	const QString downloads = QStandardPaths::writableLocation( QStandardPaths::DownloadLocation );
	ui->sourcePath->setText( downloads + "/ptrfiles" );
	ui->outputPath->setText( downloads + "/ptrfiles-compact" );

	ui->flattenButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	connect( ui->selectSource, &QToolButton::clicked, this, &PTRFlattenWidget::onSelectSource );
	connect( ui->selectOutput, &QToolButton::clicked, this, &PTRFlattenWidget::onSelectOutput );
	connect( ui->flattenButton, &QPushButton::clicked, this, &PTRFlattenWidget::onFlatten );
	connect( ui->cancelButton, &QPushButton::clicked, this, &PTRFlattenWidget::onCancel );
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
}

void PTRFlattenWidget::onSelectSource()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, "Select PTR files directory", ui->sourcePath->text(), QFileDialog::ShowDirsOnly );
	if ( !dir.isEmpty() ) ui->sourcePath->setText( dir );
}

void PTRFlattenWidget::onSelectOutput()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, "Select output directory", ui->outputPath->text(), QFileDialog::ShowDirsOnly );
	if ( !dir.isEmpty() ) ui->outputPath->setText( dir );
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
