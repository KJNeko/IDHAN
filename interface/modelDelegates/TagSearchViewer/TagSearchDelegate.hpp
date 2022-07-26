//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_TAGSEARCHDELEGATE_HPP
#define IDHAN_TAGSEARCHDELEGATE_HPP


#include <QItemDelegate>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QModelIndex>


class TagSearchDelegate : public QItemDelegate
{
Q_OBJECT

public:
	TagSearchDelegate( QObject* parent = nullptr );

	void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

	QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
};


#endif //IDHAN_TAGSEARCHDELEGATE_HPP
