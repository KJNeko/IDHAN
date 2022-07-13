//
// Created by kj16609 on 7/13/22.
//

#include "TagDelegate.hpp"

#include "TracyBox.hpp"

#include "database/tags/tags.hpp"


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
	str += QString::fromStdString( !tag.group.text.empty() ? tag.group.text : "" );
	str += QString::fromStdString( !tag.group.text.empty() ? ":" : "" );
	str += QString::fromStdString( tag.subtag.text );

	str += QString::fromStdString( " (" + std::to_string( tag_data.counter ) + ")" );

	painter->save();

	painter->drawText( option.rect, Qt::AlignCenter, str );

	painter->restore();
}


TagDelegate::TagDelegate( QObject* parent ) : QItemDelegate( parent )
{

}

