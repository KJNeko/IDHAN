//
// Created by kj16609 on 6/17/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_ImportViewer.h" resolved

// Ui
#include "ImportView.hpp"
#include "ui_ImportView.h"

// Qt
#include <QCryptographicHash>
#include <QFuture>
#include <QQueue>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

// Logging/Prof
#include "TracyBox.hpp"

// Database
#include "database/utility/databaseExceptions.hpp"
#include "database/files/metadata.hpp"

#include "idhan_systems/import/importer.hpp"
#include "idhan_systems/threading.hpp"


ImportView::ImportView( QWidget* parent ) : QWidget( parent ), ui( new Ui::ImportView )
{
	ui->setupUi( this );

	viewport = new ListViewModule( this );
	tagport = new TagViewModule( this );

	ui->viewFrame->layout()->addWidget( viewport );

	ui->tagFrame->layout()->addWidget( tagport );

	//Connect all the things
	connect( this, &ImportView::updateValues, this, &ImportView::updateValues_slot );

	//Connect the selection model to the tagview
	connect( viewport, &ListViewModule::selection, tagport, &TagViewModule::selectionChanged );
}


ImportView::~ImportView()
{

	if ( processingThread != nullptr )
	{ processingThread->cancel(); }

	delete ui;

	delete viewport;
	delete tagport;
}


void ImportView::processFiles()
{
	ZoneScoped;

	std::vector< uint64_t > file_queue;
	auto timepoint_queue_send = std::chrono::high_resolution_clock::now(); //Timepoint for the last time the queue was sent to the image list

	const uint64_t file_count { files.size() };
	uint64_t file_counter { 0 };

	using Output = std::pair< ImportResult, uint64_t >;

	//Apparently reduce needs a first parameter of a 'return' value for whatever reason
	auto reduce = [ this, &file_queue, &timepoint_queue_send, &file_counter, file_count ](
		[[maybe_unused]] uint64_t&, const Output& var ) -> void
	{
		auto processQueue = [ this, &file_queue, &timepoint_queue_send, &file_counter, file_count ]()
		{
			using namespace std::chrono_literals;

			//Check if the queue is too long, If so send it to the image list
			//Send it anyways if the previous send was longer then 30ms ago
			//Should be fine to send it every 30 ms since the flicker is caused due
			//to the signal trying to update the queue rapidly and cause a redraw for every insert
			const auto timepoint_queue_send_now { std::chrono::high_resolution_clock::now() };
			const auto time_diff { timepoint_queue_send_now - timepoint_queue_send };
			constexpr size_t queue_send_interval { 50 }; //number of items before the queue is sent anyways
			constexpr auto queue_send_time { 1000ms / 2.5 };

			if ( time_diff > queue_send_time ||
				file_queue.size() >= queue_send_interval ||
				file_counter + file_queue.size() >= file_count - 5 )
			{
				file_counter += file_queue.size();
				viewport->addFiles( file_queue );
				file_queue.clear();
				timepoint_queue_send = timepoint_queue_send_now;
			}
		};

		ZoneScoped;
		++filesProcessed;

		switch ( var.first )
		{
		case ImportResult::SUCCESS:
			++successful;
			file_queue.push_back( var.second );
			break;
		case ImportResult::ALREADY_IMPORTED:
			++alreadyinDB;
			break;
		case ImportResult::FILE_NOT_FOUND:
			++failed;
			spdlog::warn( "A file was unable to be found at the path when importing" );
			break;
		default:
			++failed;
			spdlog::error( "Reached default case in reduce function" );
			break;
		}

		processQueue();
		return;
	};

	auto process = []( std::filesystem::path path_ )
	{
		try
		{
			return importToDB( path_ );
		}
		catch ( std::exception& e )
		{
			spdlog::critical( "Process has thrown an exception: what():{}", e.what() );
			std::abort();
		}
		catch ( ... )
		{
			spdlog::critical( "Process has thrown an unhandled/uncaught exception!" );
			std::abort();
		}

		return std::make_pair< ImportResult, uint64_t >( ImportResult::UNKNOWN_ERR, 0 );
	};


	auto future = QtConcurrent::mappedReduced(
		&ImportPool::getPool(), files, process, reduce
	);

	while ( !future.isFinished() && !future.isCanceled() )
	{
		QThread::yieldCurrentThread();
		QThread::msleep( 100 );
		emit updateValues();
	}

	spdlog::info( "Finished processing files" );

	files.clear();
}


void ImportView::updateValues_slot()
{
	ZoneScoped;
	QString str;

	str += QString( "%1/%2" ).arg( filesProcessed ).arg( filesAdded );

	// Set the label
	ui->progressLabel->setText( str );

	// Set the progress bar
	ui->progressBar->setValue( static_cast<int>( filesProcessed ) );

	QString str2;

	str2 += successful ? QString( "%1 successful " ).arg( successful ) : "";
	str2 += failed && successful ? QString( "%1 failed (" ).arg( failed ) : "";
	str2 += alreadyinDB ? QString( "%1 already in DB " ).arg( alreadyinDB ) : "";
	str2 += deleted ? QString( "%1 deleted " ).arg( deleted ) : "";
	str2 += failed && successful ? QString( ")" ) : "";


	ui->statusLabel->setText( str2 );
}


void ImportView::addFiles( const std::vector< std::filesystem::path >& files_ )
{
	ZoneScoped;
	files.reserve( files_.size() );
	files = files_;

	filesAdded = files.size();

	QString str;

	str += QString( "%1/%2" ).arg( filesProcessed ).arg( filesAdded );

	// Set the label
	ui->progressLabel->setText( str );

	ui->progressBar->setMaximum( static_cast<int>( filesAdded ) );

	auto runfuture = QtConcurrent::run(
		QThreadPool::globalInstance(), &ImportView::processFiles, this
	);
}

