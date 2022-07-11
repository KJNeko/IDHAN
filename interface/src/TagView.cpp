//
// Created by kj16609 on 7/9/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagView.h" resolved

#include "TagView.hpp"
#include "ui_TagView.h"

#include <QTreeWidgetItem>
#include <QList>
#include <QStandardItemModel>
#include <QPainter>

#include "FileData.hpp"


TagView::TagView( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagView )
{
	ui->setupUi( this );

	ui->tagView->setModel( model );
	ui->tagView->setItemDelegate( delegate );
}


TagView::~TagView()
{
	delete ui;
}


void TagView::selectionChanged( const std::vector< uint64_t >& hash_ids )
{
	ZoneScoped;

	spdlog::info( "selectionChanged: {}", hash_ids.size() );

	//Clear the model
	model->reset();

	model->setFiles( hash_ids );
}


//Should only be called during invalidate
void TagModel::reset()
{
	std::lock_guard< std::mutex > lock( this->mtx );

	ZoneScoped;

	beginResetModel();
	database_ret = pqxx::result();
	endResetModel();
}


void TagModel::setFiles( const std::vector< uint64_t >& hash_ids )
{
	std::lock_guard< std::mutex > lock( this->mtx );

	ZoneScoped;


	beginResetModel();

	Connection conn;
	pqxx::work work( conn() );

	constexpr pqxx::zview query {
		"SELECT tag_id, count(tag_id) AS counter FROM mappings WHERE hash_id = ANY($1::bigint[]) GROUP BY tag_id ORDER BY count(tag_id) DESC" };

	database_ret = { work.exec_params( query, hash_ids ) };

	endResetModel();
}


TagModel::TagModel( QWidget* parent )
{

}


int TagModel::rowCount( const QModelIndex& parent ) const
{
	return database_ret.size();
}


int TagModel::columnCount( const QModelIndex& parent ) const
{
	return 1;
}


struct DataPack
{
	uint64_t tag_id { 0 };
	uint64_t counter { 0 };
};


QVariant TagModel::data( const QModelIndex& index, int role ) const
{
	DataPack pack;
	pack.tag_id = database_ret[ index.row() ][ "tag_id" ].as< uint64_t >();
	pack.counter = database_ret[ index.row() ][ "counter" ].as< uint64_t >();

	return QVariant::fromValue( pack );
}


QSize TagDelegate::sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	//50% of the width of the view
	if ( index.column() == 0 )
	{
		return QSize( option.rect.width() / 2, 25 );
	}
	else
	{
		return QSize( option.rect.width() / 4, 25 );
	}
}


void TagDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	ZoneScoped;

	auto tag_data = index.data().value< DataPack >();

	auto tag = getTag( tag_data.tag_id );

	QString str;
	str += QString::fromStdString( tag.group.text.empty() ? tag.group.text : "" );
	str += QString::fromStdString( tag.group.text.empty() ? ":" : "" );
	str += QString::fromStdString( tag.subtag.text );

	str += QString::fromStdString( " (" + std::to_string( tag_data.counter ) + ")" );

	painter->save();

	painter->drawText( option.rect, Qt::AlignCenter, str );

	painter->restore();
}


TagDelegate::TagDelegate( QObject* parent ) : QItemDelegate( parent )
{

}
