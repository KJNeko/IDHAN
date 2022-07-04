//
// Created by kj16609 on 7/3/22.
//


#pragma once
#ifndef IDHAN_THUMBNAILGENERATOR_HPP
#define IDHAN_THUMBNAILGENERATOR_HPP


#include <QCache>
#include <QFuture>
#include <QImage>
#include <QPixmapCache>

#include "TracyBox.hpp"


QPixmap getThumbnail( const uint64_t hash_id );

#endif //IDHAN_THUMBNAILGENERATOR_HPP
