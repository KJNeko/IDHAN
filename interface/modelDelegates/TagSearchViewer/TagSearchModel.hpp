//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_TAGSEARCHMODEL_HPP
#define IDHAN_TAGSEARCHMODEL_HPP


#include <QAbstractListModel>
#include <QModelIndex>
#include <QVariant>

#include <vector>

#include "DatabaseModule/DatabaseObjects/database.hpp"


class TagSearchModel : public QAbstractListModel
{
Q_OBJECT

public:
	TagSearchModel( QWidget* parent = nullptr );

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;

	int columnCount( const QModelIndex& parent = QModelIndex() ) const override;

	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;

	void reset();

	void setTags( const std::vector< uint64_t >& tags_id );

private:

	std::vector< uint64_t > tag_list;

	std::mutex mtx {};
};


#endif //IDHAN_TAGSEARCHMODEL_HPP
