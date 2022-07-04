//
// Created by kj16609 on 6/17/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_ImageModel.h" resolved

#include <QAbstractTableModel>
#include <QBuffer>
#include <QMimeDatabase>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QtConcurrent/QtConcurrent>

#include "ImageDelegate.hpp"

#include <filesystem>
#include <iostream>

#include "database/files.hpp"
#include "database/metadata.hpp"

#include "services/ThumbnailGenerator.hpp"

#include "TracyBox.hpp"


ImageDelegate::ImageDelegate( QObject* parent ) : QAbstractItemDelegate( parent )
{
}


void ImageDelegate::paint(
	QPainter* const painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	ZoneScoped;
	// Get the image
	auto hash_id = index.data( Qt::DisplayRole ).value< uint64_t >();


	auto thumbnail = getThumbnail( hash_id );
	
	QRect rect = option.rect;
	// Center the image in the rectangle
	rect.setX( rect.x() + ( rect.width() - thumbnail.width() ) / 2 );
	rect.setY( rect.y() + ( rect.height() - thumbnail.height() ) / 2 );
	rect.setWidth( thumbnail.width() );
	rect.setHeight( thumbnail.height() );

	// Draw image
	painter->save();
	// Use painter to paint the image
	painter->drawPixmap( rect, thumbnail );
	// Draw boarder around the image
	painter->drawRect( option.rect );
	painter->restore();


	return;
}


QSize ImageDelegate::sizeHint(
	[[maybe_unused]] const QStyleOptionViewItem& option, [[maybe_unused]] const QModelIndex& index ) const
{
	QSettings s;

	int width = s.value( "thumbnails/x_res", 120 ).toInt();
	int height = s.value( "thumbnails/y_res", 120 ).toInt();

	return QSize( width, height );
}


ImageModel::ImageModel( QWidget* parent ) : QAbstractListModel( parent )
{
}


QVariant ImageModel::data( const QModelIndex& index, int role ) const
{
	if ( !index.isValid() )
	{ return QVariant(); }

	if ( role == Qt::DisplayRole )
	{ return qint64( fileList[ static_cast<unsigned long>( index.row() ) ] ); }

	return QVariant();
}


void ImageModel::addImage( uint64_t id )
{
	beginInsertRows( {}, static_cast<int>(fileList.size()), static_cast<int>(fileList.size() + 1) );
	fileList.push_back( id );
	endInsertRows();
}


void ImageModel::reset()
{
	beginResetModel();
	fileList.clear();
	endResetModel();
}


int ImageModel::rowCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return static_cast<int>(fileList.size());
}


int ImageModel::columnCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return 1;
}


ImageModel::~ImageModel()
{
}