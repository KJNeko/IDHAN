//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_TAGMODEL_HPP
#define IDHAN_TAGMODEL_HPP


#include <QAbstractListModel>
#include <QModelIndex>
#include <QVariant>

#include <vector>

#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "TagData.hpp"


class TagModel : public QAbstractListModel
{
Q_OBJECT

public:
	TagModel( QWidget* parent = nullptr );

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;

	int columnCount( const QModelIndex& parent = QModelIndex() ) const override;

	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;

	void reset();

	void setTags( const std::vector< TagData >& tag_ids );

private:

	std::vector< TagData > tags;

	std::mutex mtx {};
};


#endif //IDHAN_TAGMODEL_HPP
