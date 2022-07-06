//
// Created by kj16609 on 6/16/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_ImportWindow.h" resolved

#include "importWindow.hpp"
#include "ui_importWindow.h"

#include <QDirIterator>
#include <QFileDialog>
#include <QLocale>
#include <QMimeDatabase>
#include <QStandardItemModel>
#include <iostream>

#include "mainView.hpp"


ImportWindow::ImportWindow( QWidget* parent ) : QDialog( parent ), ui( new Ui::ImportWindow )
{
	ui->setupUi( this );

	// Create a new model
	// Create a new model
	QStandardItemModel* model = new QStandardItemModel();

	model->setColumnCount( 3 );
	model->setHorizontalHeaderItem( 0, new QStandardItem( "Name" ) );
	model->setHorizontalHeaderItem( 1, new QStandardItem( "File Type" ) );
	model->setHorizontalHeaderItem( 2, new QStandardItem( "Size" ) );

	ui->fileList->setModel( model );

	ui->fileList->horizontalHeader()->setSectionResizeMode(
		0, QHeaderView::ResizeMode::Interactive
	);
	ui->fileList->horizontalHeader()->setSectionResizeMode(
		1, QHeaderView::ResizeMode::Interactive
	);
	ui->fileList->horizontalHeader()->setSectionResizeMode(
		2, QHeaderView::ResizeMode::Interactive
	);

	ui->fileList->horizontalHeader()->setStretchLastSection( false );
	ui->fileList->resizeColumnsToContents();
}


ImportWindow::~ImportWindow()
{
	delete ui;
}


void ImportWindow::on_addFolder_clicked()
{
	// Open a file dialog
	QString path = QFileDialog::getExistingDirectory( this, "Select a folder" );

	// Get all files in that path
	QDir dir( path );
	QStringList files; // = dir.entryList( QDir::Files );

	QDirIterator it( path, QDirIterator::Subdirectories );
	while ( it.hasNext() )
	{
		it.next();
		if ( it.fileInfo().isFile() )
		{ files.append( it.filePath() ); }
	}

	// Set max progress bar
	ui->progressBar->setMaximum( static_cast<int>(files.size()) );

	// Get our current locale
	QLocale locale = this->locale();

	auto model = dynamic_cast<QStandardItemModel*>( ui->fileList->model() );

	// Parse each file and see if it is a compatable mime type
	for ( auto& file: files )
	{
		QMimeDatabase db;
		QMimeType mime = db.mimeTypeForFile( file );

		QFile f( file );

		if ( f.size() == 0 )
		{
			continue;
		}

		auto sizeStr = locale.formattedDataSize( f.size() );
		
		model->appendRow(
			{ new QStandardItem( file ), new QStandardItem( mime.name() ), new QStandardItem( sizeStr ) }
		);

		// Add to the list of files for further processing
		fileList.emplace_back( file.toStdString() );

		// Set the progress bar
		ui->progressBar->setValue( ui->progressBar->value() + 1 );
	}

	ui->fileList->resizeColumnsToContents();
}


void ImportWindow::on_importNow_clicked()
{
	// Get a reference to the parent in something that we understand
	auto parent = dynamic_cast<MainWindow*>( this->parent() );

	parent->importFiles( fileList );

	this->close();
}
