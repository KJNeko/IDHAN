//
// Created by kj16609 on 7/13/22.
//

#include "TagSearchDelegate.hpp"

#include "TracyBox.hpp"

#include "DatabaseModule/tags/tags.hpp"
#include "modelDelegates/TagViewer/TagData.hpp"


QSize TagSearchDelegate::sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const
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


void TagSearchDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{

	const auto tag { index.data().value< TagData >() };

	painter->save();

	painter->drawText( option.rect, Qt::AlignLeft, tag.qt_concat() );

	painter->restore();
}


TagSearchDelegate::TagSearchDelegate( QObject* parent ) : QItemDelegate( parent )
{

}

