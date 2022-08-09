//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_TAGDELEGATE_HPP
#define IDHAN_TAGDELEGATE_HPP


#include "TagData.hpp"


#include <QItemDelegate>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QModelIndex>


class TagDelegate : public QItemDelegate
{
Q_OBJECT

public:
	TagDelegate( QObject* parent = nullptr );

	void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

	QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
};


#endif //IDHAN_TAGDELEGATE_HPP
