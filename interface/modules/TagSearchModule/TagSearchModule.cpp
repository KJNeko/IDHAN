//
// Created by kj16609 on 7/26/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagSearchModule.h" resolved

#include "TagSearchModule.hpp"
#include "ui_TagSearchModule.h"

#include "TracyBox.hpp"

#include "database/database.hpp"
#include "database/tags/tags.hpp"


#include <QStandardItemModel>


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


	Connection conn;
	auto work { conn.getWork() };

	//constexpr pqxx::zview query_search {
	//	"SELECT tag_id, (SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id) as tag_count FROM concat_tags WHERE joined_text LIKE $1 limit 15" };

	constexpr pqxx::zview query_search_count {
		"SELECT tag_id, (SELECT count(*) FROM mappings WHERE concat_tags.tag_id = mappings.tag_id) as tag_count FROM concat_tags WHERE joined_text LIKE $1 order by similarity(joined_text, $1) DESC, tag_count DESC limit 15;" };

	const std::string str = text.toStdString() + "%";

	emit updateSearchResults( work->exec_params( query_search_count, str ) );
}


void TagSearchModule::on_searchBar_returnPressed()
{
	spdlog::info( "Return pressed" );

	if ( ui->searchBar->text() == "" )
	{
		return;
	}

	//Check if the tag exists in the database
	Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query_search { "SELECT tag_id FROM concat_tags WHERE joined_text LIKE $1 limit 1" };

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

	
}


void TagSearchModule::on_searchResults_doubleClicked()
{
	spdlog::info( "Double clikced" );
}


std::string getTagStr( const uint64_t tag_id )
{
	const auto tag { getTag( tag_id ) };

	if ( tag.group == "" )
	{
		return tag.subtag.text;
	}
	else
	{
		return tag.group.text + ":" + tag.subtag.text;
	}
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