//
// Created by kj16609 on 7/7/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_ListViewport.h" resolved

#include "listViewport.hpp"
#include "ui_ListViewport.h"

#include "TracyBox.hpp"


ListViewport::ListViewport( QWidget* parent ) : QWidget( parent ), ui( new Ui::ListViewport )
{
	ui->setupUi( this );

	ui->listView->setModel( model );
	ui->listView->setItemDelegate( delegate );

	ui->listView->setWrapping( true );
	ui->listView->setFlow( QListView::Flow::LeftToRight );
	ui->listView->setSpacing( 15 );

	updateTimer.setInterval( 250 );

	connect( &updateTimer, &QTimer::timeout, this, &ListViewport::processValues );
	connect( this, &ListViewport::updateTimermsec, this, &ListViewport::updateTimermsec_slot );

	updateTimer.start();
}


ListViewport::~ListViewport()
{
	delete ui;
}


void ListViewport::resetFiles()
{
	std::lock_guard< std::mutex > lock( queue_lock );
	model->reset();
}


void ListViewport::setFiles( const std::vector< uint64_t >& files_ )
{
	std::lock_guard< std::mutex > lock( queue_lock );
	model->setFiles( files_ );
}


void ListViewport::addFile( const uint64_t file_id )
{
	/*
	 * addFile simply adds to the list of files to be processed. This helps reduce flickering and other things
	 * Two tick speeds. ('high' AKA every 16ms) and ('low' AKA every 1s) it will switch to a slow mode if the queue is empty.
	 * Inactive pages will be forced to stay in 'low' while the active pages will be in 'high' mode unless the queue is empty.
	 */

	//Lock to prevent multiple threads from adding to the queue at the same time.
	std::lock_guard< std::mutex > lock( queue_lock );
	addQueue.emplace_back( file_id );
	if ( updateTimer.interval() == 1000 && addQueue.size() >= 1 )
	{
		//Set the timer to 'high'
		//TODO make this globally configurable.
		emit updateTimermsec( 250 );
	}
}


void ListViewport::processValues()
{
	std::lock_guard< std::mutex > lock( queue_lock );
	//If queue is empty then set the timer to 'low'
	if ( addQueue.empty() )
	{
		if ( updateTimer.interval() == 250 )
		{
			emit updateTimermsec( 1000 );
		}
		//TODO make this globally configurable
	}
	else
	{
		model->addImages( addQueue );
		addQueue.clear();
	}
}


void ListViewport::updateTimermsec_slot( uint64_t msec )
{
	updateTimer.setInterval( static_cast<int>(msec) );
}