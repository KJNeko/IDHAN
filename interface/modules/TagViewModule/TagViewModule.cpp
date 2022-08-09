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


void TagViewModule::selectionChanged( const std::vector< uint64_t >& hash_ids )
{

	//Clear the model
	//model->reset();

	//Get a list of all the tags

	const UniqueConnection conn;
	pqxx::work work { *conn.connection };


	constexpr pqxx::zview query_raw {
		"SELECT tag_id, count(tag_id) AS counter, joined_text FROM mappings NATURAL JOIN concat_tags WHERE hash_id = ANY($1::bigint[]) GROUP BY tag_id, joined_text" };

	const pqxx::result result = work.exec_params( query_raw, hash_ids );

	std::vector< TagData > tags;
	for ( const auto& row: result )
	{
		const auto tag_id = row[ 0 ].as< uint64_t >();
		const auto counter = row[ 1 ].as< uint64_t >();
		const auto joined_text = row[ 2 ].as< std::string >();
		const auto tag_data = TagData { tag_id, counter, joined_text };


		tags.push_back( tag_data );
	}

	model->setTags( tags );
}





