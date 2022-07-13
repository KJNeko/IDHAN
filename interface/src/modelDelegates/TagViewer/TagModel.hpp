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

#include "database/database.hpp"


class TagModel : public QAbstractListModel
{
Q_OBJECT

public:
	TagModel( QWidget* parent = nullptr );

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;

	int columnCount( const QModelIndex& parent = QModelIndex() ) const override;

	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;

	void reset();

	//TODO implement these two if needed
	//void addTag( const uint64_t tag_id );

	//void removeTag( const uint64_t tag_id );

	void setFiles( const std::vector< uint64_t >& hash_ids );

private:

	pqxx::result database_ret;

	std::mutex mtx;
};


#endif //IDHAN_TAGMODEL_HPP
