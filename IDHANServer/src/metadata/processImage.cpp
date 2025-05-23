//
// Created by kj16609 on 5/20/25.
//

#include <QtGui/qimage.h>

#include "imageProcessing.hpp"
#include "mime/MimeInfo.hpp"

namespace idhan
{

MimeInfo processImage( std::shared_ptr< Data > data )
{
	QImage image { QImage::fromData( QByteArrayView( data->data(), data->length() ) ) };

	ImageInfo image_data {};
	image_data.resolution.width = image.width();
	image_data.resolution.height = image.height();

	MimeInfo info { image_data };

	return info;
}

} // namespace idhan