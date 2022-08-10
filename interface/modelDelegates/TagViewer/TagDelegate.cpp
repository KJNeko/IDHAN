//
// Created by kj16609 on 7/13/22.
//

#include "TagDelegate.hpp"

#include "TracyBox.hpp"

#include "DatabaseModule/tags/tags.hpp"
#include "TagData.hpp"


QSize TagDelegate::sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	//50% of the width of the view
	return QSize( option.rect.width() / ( index.column() ? 2 : 4 ), 25 );
}


void TagDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	ZoneScoped;
	const TagData tag_data { index.data( Qt::DisplayRole ).value< TagData >() };

	painter->save();

	painter->drawText( option.rect, Qt::AlignLeft, tag_data.qt_concat() );

	painter->restore();
}


TagDelegate::TagDelegate( QObject* parent ) : QItemDelegate( parent )
{

}

