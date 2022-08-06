//
// Created by kj16609 on 7/26/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagSearchModule.h" resolved

#include "TagSearchModule.hpp"
#include "ui_TagSearchModule.h"

#include "TracyBox.hpp"

#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/tags/tags.hpp"


#include <QStandardItemModel>
#include <QtConcurrent/QtConcurrent>


TagSearchModule::TagSearchModule( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagSearchModule )
{
	ui->setupUi( this );

	connect( this, &TagSearchModule::updateSearchResults, this, &TagSearchModule::updateSearch );
}


TagSearchModule::~TagSearchModule()
{
	delete ui;
}


void TagSearchModule::on_searchBar_textChanged( const QString& text )
{
	if ( text == "" )
	{
		emit updateSearchResults( pqxx::result() );
		return;
	}


	const Connection conn;
	auto work { conn.getWork() };

	//constexpr pqxx::zview query_search {
	//	"SELECT tag_id, (SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id) as tag_count FROM concat_tags WHERE joined_text LIKE $1 limit 15" };

	constexpr pqxx::zview query_search_count {
		"SELECT tag_id, (SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id) as tag_count FROM concat_tags WHERE joined_text LIKE $1 order by similarity(joined_text, $1) DESC, tag_count DESC limit 15;" };

	const std::string str = text.toStdString() + "%";

	emit updateSearchResults( work->exec_params( query_search_count, str ) );
}


std::string getTagStr( const uint64_t tag_id )
{
	const auto tag { tags::getTag( tag_id ) };

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

	if ( ui->searchBar->text() == "" )
	{
		return;
	}

	//Check if the tag exists in the database
	const Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query_search { "SELECT tag_id FROM concat_tags WHERE joined_text = $1 limit 1" };

	const pqxx::result res { work->exec_params( query_search, ui->searchBar->text().toStdString() ) };

	if ( res.empty() )
	{
		return;
	}

	spdlog::info(
		"Found tag {} with id {}", ui->searchBar->text().toStdString(), res[ 0 ][ "tag_id" ].as< uint64_t >()
	);

	//Insert to the search list
	const uint64_t tag_id = res[ 0 ][ "tag_id" ].as< uint64_t >();

	ui->activeTags->addItem( QString::fromStdString( getTagStr( tag_id ) ) );

	ui->searchBar->clear();

	QtConcurrent::run( QThreadPool::globalInstance(), &TagSearchModule::updateTagSearch, this );
}


void TagSearchModule::on_activeTags_doubleClicked()
{
	//Remove the double clicked tag
	const auto item = ui->activeTags->currentItem();
	ui->activeTags->removeItemWidget( item );
	delete item;

	QtConcurrent::run( QThreadPool::globalInstance(), &TagSearchModule::updateTagSearch, this );
}


void TagSearchModule::on_searchResults_doubleClicked()
{
	//Add the double clicked tag to the active tags
	const auto item = ui->searchResults->currentItem();
	ui->activeTags->addItem( item->text() );

	ui->searchBar->clear();

	QtConcurrent::run( QThreadPool::globalInstance(), &TagSearchModule::updateTagSearch, this );
}


void TagSearchModule::updateTagSearch()
{
	spdlog::info( "Starting search" );

	if ( ui->activeTags->count() == 0 )
	{
		emit searchComplete( std::vector< uint64_t >() );
		return;
	}

	std::lock_guard< std::mutex > lock { result_lock };

	auto startTime { std::chrono::high_resolution_clock::now() };

	const Connection conn;
	auto work { conn.getWork() };

	//Get the active tags
	std::vector< uint64_t > active_tags;
	for ( int i = 0; i < ui->activeTags->count(); i++ )
	{
		const auto tag_str = ui->activeTags->item( i )->text().toStdString();

		//TODO: Move this into it's own database file
		constexpr pqxx::zview concat_search { "SELECT tag_id FROM concat_tags WHERE joined_text = $1 limit 1" };

		const auto ret = work->exec_params( concat_search, tag_str );

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

	const pqxx::result res { work->exec_params( filtered_search, active_tags ) };

	std::vector< uint64_t > files;

	for ( const auto& row: res )
	{
		files.emplace_back( row[ "hash_id" ].as< uint64_t >() );
	}

	spdlog::info(
		"Finished query in {} ms", std::chrono::duration_cast< std::chrono::milliseconds >(
		std::chrono::high_resolution_clock::now() - startTime
	).count()
	);

	emit searchComplete( files );
}


void TagSearchModule::updateSearch( const pqxx::result& res )
{
	ui->searchResults->clear();

	for ( const auto& row: res )
	{
		const QString str { QString::fromStdString(
			getTagStr( row[ "tag_id" ].as< uint64_t >() ) +
				"(" +
				std::to_string( row[ "tag_count" ].as< uint64_t >() ) +
				")"
		) };
		ui->searchResults->addItem( new QListWidgetItem( str ) );
	}
}

