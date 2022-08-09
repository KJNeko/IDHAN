//
// Created by kj16609 on 6/17/22.
//

#pragma once
#ifndef MAIN_IMAGEMODEL_HPP
#define MAIN_IMAGEMODEL_HPP


#include <QAbstractItemDelegate>
#include <QAbstractListModel>
#include <QPainter>

#include "DatabaseModule/files/files.hpp"
#include "DatabaseModule/tags/tags.hpp"
#include "filedata/FileData.hpp"


class ImageDelegate : public QAbstractItemDelegate
{
Q_OBJECT

public:
	ImageDelegate( QObject* parent = nullptr );

	void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

	QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
};


class ImageModel : public QAbstractListModel
{
Q_OBJECT

public:
	ImageModel( QWidget* parent = nullptr );

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;

	int columnCount( const QModelIndex& parent = QModelIndex() ) const override;

	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;

	void addImages( const std::vector< uint64_t >& ids );

	void setFiles( const std::vector< FileData >& ids );

	void reset();

	std::vector< uint64_t > getFileIDs() const;

private:
	std::vector< FileData > fileList {};
};


#endif // MAIN_IMAGEMODEL_HPP
