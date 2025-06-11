//
// Created by kj16609 on 5/20/25.
//
#pragma once

#include <cstdint>

#include "FileMappedData.hpp"
#include "mime/MimeInfo.hpp"

namespace idhan
{

struct ImageData
{
	std::size_t m_width;
	std::size_t m_height;
};

MimeInfo processImage( std::shared_ptr< FileMappedData > data );

} // namespace idhan
