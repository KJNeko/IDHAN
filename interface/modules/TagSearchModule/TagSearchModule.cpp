//
// Created by kj16609 on 7/26/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagSearchModule.h" resolved

#include "TagSearchModule.hpp"
#include "ui_TagSearchModule.h"

#include "TracyBox.hpp"

#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/tags/tags.hpp"

#include "modelDelegates/TagViewer/TagDelegate.hpp"
#include "modelDelegates/TagViewer/TagModel.hpp"
#include "filedata/FileData.hpp"


#include <QStandardItemModel>
#include <QtConcurrent/QtConcurrent>


TagSearchModule::TagSearchModule( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagSearchModule )
{
	ui->setupUi( this );

	connect( this, &TagSearchModule::updateSearchResults, this, &TagSearchModule::updateSearch );

	ui->searchResults->setModel( new TagModel( this ) );
	ui->searchResults->setItemDelegate( new TagDelegate( this ) );
}


TagSearchModule::~TagSearchModule()
{
	delete ui;
}


void TagSearchModule::on_searchBar_textChanged( const QString& text )
{
	ZoneScoped;

	if ( text == "" )
	{
		emit updateSearchResults( pqxx::result() );
		return;
	}

	UniqueConnection conn;
	pqxx::work work { *( conn.connection ) };

	//constexpr pqxx::zview query_search {
	//	"SELECT tag_id, (SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id) as tag_count FROM concat_tags WHERE joined_text LIKE $1 limit 15" };

	constexpr pqxx::zview query_search_count {
		"SELECT tag_id, (SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id) as tag_count, joined_text as tag_text FROM concat_tags WHERE joined_text LIKE $1 order by similarity(joined_text, $1) DESC, tag_count DESC limit 15;" };

	constexpr pqxx::zview query_filtered {
		"SELECT tag_id, ( SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id AND hash_id = ANY( $2::bigint[] ) ) as tag_count, joined_text as tag_text FROM concat_tags WHERE tag_id IN ( SELECT tag_id FROM mappings WHERE hash_id = ANY( $2::bigint[] ) ) AND joined_text LIKE $1 order by similarity(joined_text, $1) DESC, tag_count DESC limit 15;" };

	const std::string str { text.toStdString() + "%" };

	if ( ui->activeTags->count() == 0 )
	{
		emit updateSearchResults( work.exec_params( query_search_count, str ) );
	}
	else
	{
		emit updateSearchResults( work.exec_params( query_filtered, str, previous_result ) );
	}

}


std::string getTagStr( const uint64_t tag_id )
{
	ZoneScoped;

	const auto tag_future { tags::async::getTag( tag_id ) };

	const auto tag { tag_future.result() };

	if ( tag.group == "" )
	{
		return tag.subtag;
	}
	else
	{
		return tag.group + ":" + tag.subtag;
	}
}


void TagSearchModule::on_searchBar_returnPressed()
{
	ZoneScoped;

	if ( ui->searchBar->text() == "" )
	{
		return;
	}

	//Check if the tag exists in the database
	UniqueConnection conn;
	pqxx::work work { *( conn.connection ) };

	constexpr pqxx::zview query_search { "SELECT tag_id FROM concat_tags WHERE joined_text = $1 limit 1" };

	const pqxx::result res { work.exec_params( query_search, ui->searchBar->text().toStdString() ) };

	if ( res.empty() )
	{
		return;
	}

	//Insert to the search list
	const uint64_t tag_id = res[ 0 ][ "tag_id" ].as< uint64_t >();

	ui->activeTags->addItem( QString::fromStdString( getTagStr( tag_id ) ) );

	ui->searchBar->clear();

	const auto future { QtConcurrent::run( QThreadPool::globalInstance(), &TagSearchModule::updateTagSearch, this ) };
}


void TagSearchModule::on_activeTags_doubleClicked()
{
	ZoneScoped;

	//Remove the double clicked tag
	const auto item = ui->activeTags->currentItem();
	ui->activeTags->removeItemWidget( item );
	delete item;

	const auto future { QtConcurrent::run( QThreadPool::globalInstance(), &TagSearchModule::updateTagSearch, this ) };
}


void TagSearchModule::on_searchResults_doubleClicked()
{
	ZoneScoped;

	//Add the double clicked tag to the active tags
	const auto item_index = ui->searchResults->currentIndex();

	ui->activeTags->addItem( QString::fromStdString( item_index.data( Qt::DisplayRole ).value< TagData >().tagText ) );

	ui->searchBar->clear();

	const auto future { QtConcurrent::run( QThreadPool::globalInstance(), &TagSearchModule::updateTagSearch, this ) };
}


void TagSearchModule::updateTagSearch()
{
	ZoneScoped;

	if ( ui->activeTags->count() == 0 )
	{
		emit searchComplete( std::vector< FileData >() );
		return;
	}

	std::lock_guard< std::mutex > lock { result_lock };

	auto startTime { std::chrono::high_resolution_clock::now() };

	UniqueConnection conn;
	pqxx::work work { *( conn.connection ) };

	//Get the active tags
	std::vector< uint64_t > active_tags;
	for ( int i = 0; i < ui->activeTags->count(); i++ )
	{
		const auto tag_str = ui->activeTags->item( i )->text().toStdString();

		//TODO: Move this into it's own database file
		constexpr pqxx::zview concat_search { "SELECT tag_id FROM concat_tags WHERE joined_text = $1 limit 1" };

		const auto ret = work.exec_params( concat_search, tag_str );

		if ( ret.size() == 0 )
		{
			continue;
		}

		active_tags.push_back( ret[ 0 ][ "tag_id" ].as< uint64_t >() );
	}

	//Get the search results
	//TODO:Move this into files.hpp

	constexpr pqxx::zview filtered_search(
		"SELECT hash_id FROM (SELECT hash_id, array_agg(tag_id) AS tags FROM mappings group by hash_id) as temp_query WHERE temp_query.tags @> $1"
	);

	const pqxx::result res { work.exec_params( filtered_search, active_tags ) };

	std::vector< uint64_t > files;

	for ( const auto& row: res )
	{
		files.emplace_back( row[ "hash_id" ].as< uint64_t >() );
	}

	//Set the internal list to the search results
	previous_result = files;

	std::vector< FileData > file_data;

	for ( const auto& file: files )
	{
		file_data.emplace_back( file );
	}

	emit searchComplete( file_data );
}


void TagSearchModule::updateSearch( const pqxx::result& res )
{
	ZoneScoped;

	ui->searchResults->reset();

	std::vector< TagData > data_set;

	for ( const auto& row: res )
	{
		TagData tag_data;
		tag_data.tagText = row[ "tag_text" ].as< std::string >();
		tag_data.tag_id = row[ "tag_id" ].as< uint64_t >();
		tag_data.counter = row[ "tag_count" ].as< uint64_t >();

		data_set.push_back( tag_data );
	}

	auto model_ptr { reinterpret_cast<TagModel*>(ui->searchResults->model()) };

	model_ptr->setTags( data_set );
}

