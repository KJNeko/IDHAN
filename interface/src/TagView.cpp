//
// Created by kj16609 on 7/9/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagView.h" resolved

#include "TagView.hpp"
#include "ui_TagView.h"

#include <QTreeWidgetItem>

#include "FileData.hpp"


TagView::TagView( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagView )
{
	ui->setupUi( this );
}


TagView::~TagView()
{
	delete ui;
}


struct GroupMember
{
	Group group;
	std::vector< std::pair< Subtag, uint64_t > > subtags;
};


void TagView::setTags( const std::vector< Tag >& tags )
{
	ZoneScoped;

	std::vector< GroupMember > list;

	for ( const Tag& tag: tags )
	{
		//See if it's already in the list
		auto iter = std::find_if(
			list.begin(), list.end(), [ &tag ]( const GroupMember& member )
		{
			return member.group == tag.group;
		}
		);

		if ( iter == list.end() )
		{
			//It's not in the list, add it
			GroupMember member;
			member.group = tag.group;
			member.subtags.emplace_back( std::make_pair( tag.subtag, 1 ) );
			list.push_back( member );
		}
		else
		{
			//If it's in the list check if the subtag exists
			auto subtag_iter = std::find_if(
				iter->subtags.begin(), iter->subtags.end(), [ &tag ]( const std::pair< Subtag, uint64_t >& subtag )
			{
				return subtag.first == tag.subtag;
			}
			);

			if ( subtag_iter == iter->subtags.end() )
			{
				//It's not in the list, add it
				iter->subtags.emplace_back( std::make_pair( tag.subtag, 1 ) );
			}
			else
			{
				//It's in the list, increment the count
				subtag_iter->second++;
			}
		}
	}

	//Sort the list based on group name

	std::sort(
		list.begin(), list.end(), []( const GroupMember& a, const GroupMember& b )
	{
		return a.group.text < b.group.text;
	}
	);

	//Sort each internal list by
	for ( GroupMember& member: list )
	{

		std::sort(
			member.subtags.begin(), member.subtags.end(), [](
			const std::pair< Subtag, uint64_t >& a, const std::pair< Subtag, uint64_t >& b )
		{
			return a.first.text < b.first.text;
		}
		);

	}

	//Reset the viewer
	ui->tagList->clear();

	//Add the tags
	for ( const GroupMember& member: list )
	{
		QTreeWidgetItem* group_item = new QTreeWidgetItem( ui->tagList );
		group_item->setText( 0, member.group.text.c_str() );
		group_item->setExpanded( true );

		for ( const std::pair< Subtag, uint64_t >& subtag: member.subtags )
		{
			QTreeWidgetItem* subtag_item = new QTreeWidgetItem( group_item );
			subtag_item->setText( 0, ( subtag.first.text + " (" + std::to_string( subtag.second ) + ")" ).c_str() );
		}
	}
}


void TagView::selectionChanged( const std::vector< uint64_t >& hash_ids )
{
	ZoneScoped;
	std::vector< Tag > tags;

	for ( const auto hash_id: hash_ids )
	{
		const auto data_ptr { FileData( hash_id ) };
		tags.reserve( data_ptr->tags.size() );

		tags.insert( tags.end(), data_ptr->tags.begin(), data_ptr->tags.end() );
	}

	this->setTags( tags );
}




