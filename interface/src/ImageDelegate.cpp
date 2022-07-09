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
#include <QPixmapCache>

#include "ImageDelegate.hpp"

#include <filesystem>

#include "TracyBox.hpp"


#include "database/databaseExceptions.hpp"
#include "database/tags.hpp"
#include "database/files.hpp"

#include "FileData.hpp"


ImageDelegate::ImageDelegate( QObject* parent ) : QAbstractItemDelegate( parent )
{
}


void ImageDelegate::paint(
	QPainter* const painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	ZoneScoped;
	// Get the image
	const FileData filedat { index.data( Qt::DisplayRole ).value< FileData >() };

	QPixmap thumbnail;
	if ( const auto key = filedat->sha256.getQByteArray().toHex(); !QPixmapCache::find( key, &thumbnail ) )
	{
		ZoneScopedN( "Get image" );

		auto thumbnail_path = filedat->thumbnail_path;

		if ( !std::filesystem::exists( thumbnail_path ) )
		{
			//spdlog::warn( "Expected thumbnail at {}. It is missing!", thumbnail_path.string() );
			thumbnail = QPixmap( ":/IDHAN/icon-64.png" );
		}
		else
		{
			ZoneScopedN( "Load image" );
			thumbnail.load( QString::fromStdString( thumbnail_path.string() ) );
			QPixmapCache::insert( key, thumbnail );
		}
	}

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

	//See if we have "system:inbox"
	{
		ZoneScopedN( "search_tag" )
		for ( const auto& [ group, subtag ]: filedat->tags )
		{
			if ( group == "system" && subtag == "inbox" )
			{
				//Add the inbox symbol to the top left of the image.
				const QPixmap inbox_symbol( ":/IDHAN/letter-16.png" );
				painter->drawPixmap( option.rect.topLeft(), inbox_symbol );
				break;
			}
		}
	}
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
	{
		return QVariant::fromValue( fileList[ static_cast<unsigned long>( index.row() ) ] );
	}

	return QVariant();
}


void ImageModel::addImages( const std::vector< uint64_t >& queue )
{
	beginInsertRows( {}, static_cast<int>(fileList.size()), static_cast<int>(fileList.size() + queue.size()) );

	fileList.reserve( queue.size() );

	for ( auto& hash_id: queue )
	{
		fileList.emplace_back( hash_id );
	}

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


void ImageModel::setFiles( const std::vector< uint64_t >& ids )
{
	beginResetModel();
	fileList.reserve( ids.size() );
	for ( auto& id: ids )
	{
		fileList.emplace_back( id );
	}
	endResetModel();
}
