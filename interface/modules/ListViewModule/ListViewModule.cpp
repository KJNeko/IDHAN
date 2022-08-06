//
// Created by kj16609 on 7/7/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_ListViewport.h" resolved

#include "ListViewModule.hpp"
#include "ui_ListViewModule.h"

#include "TracyBox.hpp"


ListViewModule::ListViewModule( QWidget* parent ) : QWidget( parent ), ui( new Ui::ListViewModule )
{
	ui->setupUi( this );

	ui->listView->setModel( model );
	ui->listView->setItemDelegate( delegate );

	ui->listView->setSpacing( 5 );

	connect(
		ui->listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ListViewModule::itemActivated
	);

}


ListViewModule::~ListViewModule()
{
	delete ui;
}


void ListViewModule::resetFiles()
{
	ZoneScoped;
	std::lock_guard< std::mutex > lock( queue_lock );
	model->reset();
}


void ListViewModule::setFiles( const std::vector< uint64_t >& files_ )
{
	ZoneScoped;
	spdlog::info( "Files setting" );

	if ( files_.empty() )
	{
		resetFiles();
		return;
	}

	std::lock_guard< std::mutex > lock( queue_lock );
	model->setFiles( files_ );
}


void ListViewModule::addFile( const uint64_t file_id )
{
	ZoneScoped;
	model->addImages( { file_id } );

	//If there are no files selected then update the model
	if ( ui->listView->selectionModel()->selectedIndexes().empty() )
	{
		emit selection( model->getFileIDs() );
	}
}


void ListViewModule::addFiles( const std::vector< uint64_t >& file_id )
{
	ZoneScoped;
	model->addImages( file_id );

	//If there are no files selected then update the model
	if ( ui->listView->selectionModel()->selectedIndexes().empty() )
	{
		emit selection( model->getFileIDs() );
	}
}


void ListViewModule::itemActivated(
	[[maybe_unused]] const QItemSelection& selected, [[maybe_unused]] const QItemSelection& deselected )
{
	ZoneScoped;

	auto list = ui->listView->selectionModel()->selectedIndexes();

	std::vector< uint64_t > files;
	files.reserve( static_cast<unsigned long>(list.size()) );

	for ( const auto& i: list )
	{
		files.push_back( model->data( i, Qt::DisplayRole ).value< uint64_t >() );
	}

	//If there is no files then return everything
	if ( files.empty() )
	{
		emit selection( model->getFileIDs() );
		return;
	}

	emit selection( files );
}
