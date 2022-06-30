//
// Created by kj16609 on 6/17/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_ImportViewer.h" resolved

#include "importViewer.hpp"
#include "ui_importViewer.h"

#include <QCryptographicHash>
#include <QFile>
#include <QQueue>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include "TracyBox.hpp"

#include "database/files.hpp"

ImportViewer::ImportViewer( QWidget* parent )
	: QWidget( parent ),
	  ui( new Ui::ImportViewer )
{
	ui->setupUi( this );

	connect( this, &ImportViewer::updateValues, this, &ImportViewer::updateValues_slot );
}

ImportViewer::~ImportViewer()
{

	if ( processingThread != nullptr ) { processingThread->cancel(); }


	delete ui;
}

struct FileData
{
	std::string path;
	std::string mime;

	QByteArray filedata;
	QByteArray sha256;
	uint64_t id;

	QVector<QPair<QString, QString>> tags;
};

void ImportViewer::processFiles()
{
	ZoneScoped;
	std::vector<QPromise<FileData>> promises;
	std::vector<QFuture<FileData>> futures;

	promises.resize( files.size() );

	TracyCZoneN( promiseZone, "Promise initalization", true );
	for ( auto& promise : promises )
	{
		QFuture<FileData> future = promise.future();
		futures.emplace_back(
			future
				.then(
					QtFuture::Launch::Async,
					[]( FileData data_ )
					{
						data_.sha256 = QCryptographicHash::hash(
							data_.filedata, QCryptographicHash::Sha256 );
						return data_;
					} )
				.then(
					QtFuture::Launch::Async,
					[]( FileData data_ )
					{
						// Insert into database
						Hash sha256;
						memcpy( sha256.data(), data_.sha256.data(), sha256.size() );

						uint64_t id = getFileID( sha256, true );

						spdlog::info( "Inserted file with id {}", id );

						return data_;
					} ) );
	}
	TracyCZoneEnd( promiseZone );

	auto runfuture = QtConcurrent::run(
		QThreadPool::globalInstance(),
		[ this, &promises ]
		{
			for ( size_t i = 0; i < files.size(); ++i )
			{
				ZoneScopedN( "File processing" );
				auto& promise = promises[ i ];

				FileData data_;

				auto file = files.at( i );

				data_.path = file.first;
				data_.mime = file.second;

				promise.start();

				// Load file from desk
				QFile f( QString::fromStdString( data_.path ) );
				if ( !f.open( QIODevice::ReadOnly ) )
				{
					spdlog::error( "Could not open file {}", data_.path );
					continue;
				}

				data_.filedata = f.readAll();
				f.close();

				promise.addResult( data_ );
				promise.finish();
			}
		} );

	for ( auto& future : futures )
	{
		auto data_ = future.result();
		++filesProcessed;

		emit updateValues();
	}
}

void ImportViewer::updateValues_slot()
{
	QString str;

	str += QString( "%1/%2" ).arg( filesProcessed ).arg( filesAdded );

	// Set the label
	ui->progressLabel->setText( str );

	// Set the progress bar
	ui->progressBar->setValue( filesProcessed );
}

void ImportViewer::addFiles( const std::vector<std::pair<std::string, std::string>>& files_ )
{
	// File import list
	for ( auto& file : files_ ) { files.push_back( file ); }

	filesAdded = files.size();

	QString str;

	str += QString( "%1/%2" ).arg( filesProcessed ).arg( filesAdded );

	// Set the label
	ui->progressLabel->setText( str );

	ui->progressBar->setMaximum( filesAdded );

	auto runfuture = QtConcurrent::run(
		QThreadPool::globalInstance(), &ImportViewer::processFiles, this );
}
