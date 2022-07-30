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

#include "database/database.hpp"

#include "modelDelegates/TagViewer/TagModel.hpp"


class TagSearchModel : public TagModel
{
Q_OBJECT

public:

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;

	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;

	void setTags( const std::vector< uint64_t >& tags_id );

private:

	std::vector< uint64_t > tag_list;

	std::mutex mtx {};
};


#endif //IDHAN_TAGSEARCHMODEL_HPP
