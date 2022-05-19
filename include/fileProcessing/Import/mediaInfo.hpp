#pragma once
#ifndef IDHAN_FILEPROCESSING_FILEINFO_HPP_INCLUDED
#define IDHAN_FILEPROCESSING_FILEINFO_HPP_INCLUDED

#include <cstddef>
#include <variant>

#include "include/FileProcessing/MrMime/filetype_enum.h"

namespace idhan::fileprocessing {

struct JPGHeader
{
	bool greyscale;
	int placeholder;
};

struct PNGHeader
{
	int placeholder;
};

// was MetadataBasic

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded" // deal with this later
struct FileInfo
{
	std::size_t file_size;
	struct resolution_t
	{ // if your resolution exceeds unsigned int, may god help you
		unsigned int width;
		unsigned int height;
	} resolution;

	std::variant<
		JPGHeader,
		PNGHeader
	> header_info;
};
#pragma GCC diagnostic pop

} // namespace idhan::fileprocessing

#endif // IDHAN_FILEPROCESSING_FILEINFO_HPP_INCLUDED
