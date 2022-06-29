//
// Created by kj16609 on 6/17/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_ImportViewer.h" resolved

#include "importViewer.hpp"
#include "ui_importViewer.h"

#include <QQueue>

ImportViewer::ImportViewer( QWidget* parent ) : QWidget( parent ), ui( new Ui::ImportViewer )
{
	ui->setupUi( this );
}

ImportViewer::~ImportViewer()
{
	delete ui;
}

void ImportViewer::addFiles( const QVector<QPair<QString, QString>>& files_ )
{
	std::lock_guard<std::mutex> lock( filesMutex );
	// File import list
	for ( auto& file : files_ )
	{
		files.push( file );
		filesAdded++;
	}

	// Set the label
	ui->progressLabel->setText( QString( "%2/%1" ).arg( filesAdded ).arg( filesProcessed ) );

	// Start the import
}
