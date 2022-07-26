//
// Created by kj16609 on 7/13/22.
//

#include "TagSearchDelegate.hpp"

#include "TracyBox.hpp"

#include "database/tags/tags.hpp"


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
	ZoneScoped;

	const auto tag = getTag( index.data().value< uint64_t >() );

	QString str;
	str += QString::fromStdString( !tag.group.text.empty() ? tag.group.text : "" );
	str += QString::fromStdString( !tag.group.text.empty() ? ":" : "" );
	str += QString::fromStdString( tag.subtag.text );

	painter->save();

	painter->drawText( option.rect, Qt::AlignLeft, str );

	painter->restore();
}


TagSearchDelegate::TagSearchDelegate( QObject* parent ) : QItemDelegate( parent )
{

}
