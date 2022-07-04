//
// Created by kj16609 on 7/3/22.
//

#include "ThumbnailGenerator.hpp"

#include <QStringView>
#include <QtConcurrent/QtConcurrent>


#include "database/files.hpp"
#include "database/databaseExceptions.hpp"
#include "database/metadata.hpp"


QPixmap generate( const uint64_t hash_id, const Hash32& sha256 )
{
	Database db = Database();

	ZoneScoped;
	const auto file_path = getFilepath( hash_id, db );

	if ( !std::filesystem::exists( file_path ) )
	{
		throw IDHANError( ErrorNo::FILE_NOT_FOUND, file_path );
	}

	const auto mime = getMime( hash_id, db );

	const QString mime_type = QString::fromStdString( mime );

	QSettings s;
	const auto x_res = s.value( "thumbnails/x_res", 120 ).toInt();
	const auto y_res = s.value( "thumbnails/y_res", 120 ).toInt();

	if ( mime_type.contains( "image/" ) )
	{
		TracyCZoneN( read_image, "read_image", true );
		QImage image_qimage;
		image_qimage.load( file_path.c_str() );
		TracyCZoneEnd( read_image );

		TracyCZoneN( resize_image, "resize_image", true );
		//Resize the image to the thumbnail size while preserving aspect ratio
		const auto resized_qimage = image_qimage.scaled( x_res, y_res, Qt::KeepAspectRatio, Qt::SmoothTransformation );
		TracyCZoneEnd( resize_image );

		TracyCZoneN( cache_image, "cache_image", true );
		//Place the image into the QPixmapCache
		QPixmapCache cache;
		TracyCZoneEnd( cache_image );

		TracyCZoneN( return_pixmap, "return_pixmap", true );
		auto hex = sha256.getQByteArray().toHex();

		//Create a pixmap from the image
		auto pixmap = QPixmap::fromImage( resized_qimage );

		cache.insert( hex, pixmap );

		TracyCZoneEnd( return_pixmap );
		return pixmap;
	}
	else
	{
		//Return white image
		return QPixmap( QSize( 100, 100 ) );
	}


}


QPixmap getThumbnail( const uint64_t hash_id )
{
	ZoneScoped;
	QPixmapCache cache;

	//Get the sha256 for the image
	Hash32 sha256 = getHash( hash_id );
	//Get hex str
	auto hex = sha256.getQByteArray().toHex();

	QPixmap pixmap;

	if ( cache.find( hex, &pixmap ) )
	{
		return pixmap;
	}
	else
	{
		ZoneScopedN( "generate_thumbnail" );
		//See if the image is on the disk
		auto path = getThumbnailpath( hash_id );


		if ( !std::filesystem::exists( path ) )
		{
			//No thumbnail
			pixmap = generate( hash_id, sha256 );
			//Save to disk

			//Ensure that the parent directory exists
			std::filesystem::create_directories( path.parent_path() );

			TracyCZoneN( save_thumbnail, "save_thumbnail", true );
			pixmap.save( path.c_str() );
			TracyCZoneEnd( save_thumbnail );
		}
		else
		{
			ZoneScopedN( "load_thumbnail" );
			//Load the image
			pixmap.load( path.string().c_str() );

			//Cache the image
			cache.insert( hex, pixmap );
		}

		return pixmap;
	}

	return QPixmap();
}


