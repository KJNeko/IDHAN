//
// Created by kj16609 on 7/13/22.
//

#include "TagDelegate.hpp"

#include "TracyBox.hpp"

#include "DatabaseModule/tags/tags.hpp"
#include "DataPack.hpp"


QSize TagDelegate::sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	//50% of the width of the view
	return QSize( option.rect.width() / ( index.column() ? 2 : 4 ), 25 );
}


void TagDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	ZoneScoped;

	const auto [ hash_id, count ] = index.data().value< DataPack >();

	const auto tag_future { tags::async::getTag( hash_id ) };

	const auto tag { tag_future.result() };

	const QString group_text { QString::fromStdString( !tag.group.empty() ? tag.group + ":" : "" ) };
	const QString subtag_text { QString::fromStdString( tag.subtag ) };
	const QString str { group_text + subtag_text };

	painter->save();

	painter->drawText( option.rect, Qt::AlignLeft, str );

	painter->restore();
}


TagDelegate::TagDelegate( QObject* parent ) : QItemDelegate( parent )
{

}

