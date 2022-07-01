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

#include "TracyBox.hpp"


ImageDelegate::ImageDelegate( QObject* parent )
	: QAbstractItemDelegate( parent )
{
}

struct DelegateData
{
	uint64_t hash_id;

	Hash32 sha256;
	std::string sha256_hex;

	std::filesystem::path thumbnail_path;
	std::filesystem::path image_path;

	bool thumb_valid {false};

	DelegateData() = default;

	DelegateData( uint64_t hash_id_ ) : sha256(getHash(hash_id_))
	{
		// Check that the thumbnail is generate and ready to read
		thumbnail_path = getThubmnailpath( hash_id_ );
	}

	[[nodiscard]] DelegateData(const DelegateData& other) noexcept = default;

};

void ImageDelegate::paint(
	QPainter* const painter,
	const QStyleOptionViewItem& option,
	const QModelIndex& index ) const
{
	ZoneScoped;
	// Get the image
	auto data = index.data( Qt::DisplayRole ).value<DelegateData>();

	if(data.thumb_valid)
	{
		// Load QImage from disk
		QImage image_qimage;
		image_qimage.load( data.thumbnail_path.string().c_str() );

		QRect rect = option.rect;
		// Center the image in the rectangle
		rect.setX( rect.x() + ( rect.width() - image_qimage.width() ) / 2 );
		rect.setY( rect.y() + ( rect.height() - image_qimage.height() ) / 2 );
		rect.setWidth( image_qimage.width() );
		rect.setHeight( image_qimage.height() );

		// Draw image
		painter->save();
		// Use painter to paint the image
		painter->drawImage( rect, image_qimage );
		// Draw boarder around the image
		painter->drawRect( option.rect );
		painter->restore();
	}



	return;
}

QSize ImageDelegate::sizeHint(
	[[maybe_unused]] const QStyleOptionViewItem& option,
	[[maybe_unused]] const QModelIndex& index ) const
{
	QSettings s;

	int width  = s.value( "thumbnails/x_res" ).toInt();
	int height = s.value( "thumbnails/y_res" ).toInt();

	return QSize( width, height );
}


ImageModel::ImageModel( QWidget* parent ) : QAbstractListModel( parent )
{
}

QVariant ImageModel::data( const QModelIndex& index, int role ) const
{
	ZoneScoped;
	if ( !index.isValid() ) { return QVariant(); }

	if ( role == Qt::DisplayRole ) { return qint64( fileList[ static_cast<unsigned long>( index.row() ) ] ); }

	return QVariant();
}

void ImageModel::addImage( uint64_t id )
{
	ZoneScoped;
	beginInsertRows( {}, static_cast<int>(fileList.size()), static_cast<int>(fileList.size() + 1) );
	fileList.push_back( id );
	endInsertRows();
}

void ImageModel::reset()
{
	ZoneScoped;
	fileList.clear();
}

int ImageModel::rowCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	ZoneScoped;
	return static_cast<int>(fileList.size());
}

int ImageModel::columnCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	ZoneScoped;
	return 1;
}

ImageModel::~ImageModel()
{
	ZoneScoped;
}

void ImageModel::generateThumbnail( uint64_t hash_id_ )
{
	ZoneScoped;

	QtConcurrent::run(
		QThreadPool::globalInstance(),
		[ this ]( uint64_t hash_id )
		{
			QSettings s;

			std::filesystem::path thumbnail_path =
				s.value( "paths/thumbnail_path" ).toString().toStdString();
			std::filesystem::path file_path =
				s.value( "paths/file_path" ).toString().toStdString();

			// Find the two filepaths we want
			auto sha256 = getHash( hash_id );

			// Convert to hex
			QByteArray hash_bytes;
			hash_bytes.resize( sha256.size() );
			memcpy( hash_bytes.data(), sha256.data(), sha256.size() );

			auto hex = hash_bytes.toHex().toStdString();


			thumbnail_path /= "t";
			thumbnail_path += hex.substr( 0, 2 );
			thumbnail_path /= hex + ".jpg";

			try
			{
				file_path /= "f";
				file_path += hex.substr( 0, 2 );
				file_path /= hex + "." + getFileExtention( hash_id );
			}
			catch ( ... )
			{
				return;
			}

			QMimeDatabase mime_db;
			if ( !mime_db
					  .mimeTypeForFile(
						  QString::fromStdString( file_path.string() ),
						  QMimeDatabase::MatchMode::MatchContent )
					  .name()
					  .contains( "image/" ) )
			{
				spdlog::warn( "Not supported file type for thumbnail generation" );
				return;
			}


			if ( !std::filesystem::exists( file_path ) )
			{
				spdlog::warn( "File does not exist for {}", file_path.string() );
				return;
			}

			// Check if the thumbnail already exists
			if ( !std::filesystem::exists( thumbnail_path ) )
			{
				// Create the thumbnail
				QImage image_qimage;
				image_qimage.load( file_path.string().c_str() );

				if ( image_qimage.isNull() )
				{
					spdlog::warn( "Failed to load image {}", file_path.string() );
					return;
				}

				image_qimage = image_qimage.scaled(
					s.value( "thumbnails/x_res", 100 ).toInt(),
					s.value( "thumbnails/y_res", 125 ).toInt(),
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation );

				// Ensure parent directory is made
				std::filesystem::create_directories( thumbnail_path.parent_path() );

				// Save the thumbnail
				image_qimage.save( thumbnail_path.string().c_str() );

				spdlog::info(
					"Generated thumbnail for {} at {}",
					file_path.string(),
					thumbnail_path.string() );
			}

			// Get the index for the item we are working on via the hash_id
			auto iter = std::find_if(
				fileList.begin(),
				fileList.end(),
				[ & ]( uint64_t id ) { return id == hash_id; } );

			// If the item is found, emit the signal to update the view
			if ( iter != fileList.end() )
			{
				dataChanged(
					index( iter - fileList.begin() ),
					index( iter - fileList.begin() ) );
			}
		},
		hash_id_ );

	return;
}
