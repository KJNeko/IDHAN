//
// Created by kj16609 on 7/9/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagView.h" resolved

#include "TagViewModule.hpp"
#include "ui_TagViewModule.h"

#include <QTreeWidgetItem>
#include <QList>
#include <QStandardItemModel>
#include <QPainter>

#include "filedata/FileData.hpp"

#include <QtConcurrent/QtConcurrent>


TagViewModule::TagViewModule( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagViewModule )
{
	ui->setupUi( this );

	ui->tagView->setModel( model );
	ui->tagView->setItemDelegate( delegate );

	//Set alignment left
	ui->tagView->setItemAlignment( Qt::AlignLeft );
}


TagViewModule::~TagViewModule()
{
	delete ui;
}


void TagViewModule::updateTagsFromFiles( const std::vector< uint64_t >& hash_ids )
{
	ZoneScoped;

	//Clear the model
	//model->reset();

	//Get a list of all the tags

	std::vector< FileData > files;
	files.reserve( hash_ids.size() );

	TracyCZoneN( get_files, "Fetch files", true );
	for ( const auto& hash_id: hash_ids )
	{
		files.emplace_back( hash_id );
	}
	TracyCZoneEnd( get_files );


	TracyCZoneN( counter, "Count tags", true );

	std::unordered_map< uint64_t, TagData > tag_data;
	tag_data.reserve( files.size() );

	for ( const auto& file: files )
	{
		for ( const auto& tag: file->tags )
		{
			auto& counter = tag_data[ tag.tag_id ].counter;
			++counter;
			if ( counter <= 1 )
			{
				std::string str = tag.group;
				str += ":";
				str += tag.subtag;
				tag_data[ tag.tag_id ].tagText = tag.group + ":" + tag.subtag;
			}
		}
	}

	TracyCZoneEnd( counter );

	TracyCZoneN( translate, "map -> vector", true );
	std::vector< TagData > temp_data;
	temp_data.reserve( tag_data.size() );
	std::copy( tag_data.begin(), tag_data.end(), std::back_inserter( temp_data ) );
	TracyCZoneEnd( translate );


	TracyCZoneN( sort, "Sort", true );
	std::sort(
		temp_data.begin(), temp_data.end(), []( const TagData& a, const TagData& b )
	{
		return a.counter > b.counter;
	}
	);
	TracyCZoneEnd( sort );

	model->setTags( temp_data );
}


void TagViewModule::selectionChanged( const std::vector< uint64_t >& hash_ids )
{
	QtConcurrent::run( &TagViewModule::updateTagsFromFiles, this, hash_ids );
}





