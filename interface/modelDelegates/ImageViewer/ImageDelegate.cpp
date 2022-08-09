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


#include "DatabaseModule/utility/databaseExceptions.hpp"

#include "filedata/FileData.hpp"


ImageDelegate::ImageDelegate( QObject* parent ) : QAbstractItemDelegate( parent )
{
}


void ImageDelegate::paint(
	QPainter* const painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	// Get the image
	const FileData filedat { index.data( Qt::DisplayRole ).value< uint64_t >() };

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

	//If selected paint a light blue background
	if ( option.state & QStyle::State_Selected )
	{
		painter->fillRect( option.rect, QColor( 0, 0, 255, 50 ) );
	}
}


QSize ImageDelegate::sizeHint(
	[[maybe_unused]] const QStyleOptionViewItem& option, [[maybe_unused]] const QModelIndex& index ) const
{
	QSettings s( QSettings::IniFormat, QSettings::UserScope, "Future Gadget Labs", "IDHAN" );

	int width = s.value( "thumbnails/x_res", 120 ).toInt();
	int height = s.value( "thumbnails/y_res", 120 ).toInt();

/*	const FileData filedat { index.data( Qt::DisplayRole ).value< uint64_t >() };

	if ( filedat->thumbnail_path.empty() )
	{
		return QSize( width, height );
	}
	else
	{
		//Load the image and get the size
		QPixmap thumbnail;
		thumbnail.load( QString::fromStdString( filedat->thumbnail_path.string() ) );
		return { width, thumbnail.size().height() };
	}
*/
	return QSize( width, height );
}


ImageModel::ImageModel( QWidget* parent ) : QAbstractListModel( parent )
{
}


QVariant ImageModel::data( const QModelIndex& index, int role ) const
{
	if ( !index.isValid() )
	{ return {}; }

	if ( role == Qt::DisplayRole )
		return QVariant::fromValue( fileList[ static_cast<unsigned long>( index.row() ) ]->hash_id );
	else return {};
}


void ImageModel::addImages( const std::vector< uint64_t >& queue )
{
	beginInsertRows( {}, static_cast<int>(fileList.size()), static_cast<int>(fileList.size() + queue.size()) );

	fileList.reserve( queue.size() );

	for ( const auto& hash_id: queue )
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


void ImageModel::setFiles( const std::vector< FileData >& file_data )
{
	ZoneScoped;
	beginResetModel();
	fileList = file_data;
	endResetModel();
}


std::vector< uint64_t > ImageModel::getFileIDs() const
{
	std::vector< uint64_t > ret;

	ret.reserve( fileList.size() );

	for ( const auto& file: fileList )
	{
		ret.push_back( file->hash_id );
	}

	return ret;
}
