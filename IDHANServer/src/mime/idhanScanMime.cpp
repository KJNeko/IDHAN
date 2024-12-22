//
// Created by kj16609 on 12/18/24.
//

#include "idhanScanMime.hpp"

#include <functional>
#include <optional>

#include "MimeInfo.hpp"

enum MimeTypes
{
	IMAGE_JPEG,
	IMAGE_GIF,
	IMAGE_PNG,
	IMAGE_APNG,
	IMAGE_AVIF,
	IMAGE_WEBP,

	VIDEO_MP4,
	VIDEO_MPEG,
	VIDEO_WEBM,
};

namespace idhan::mime
{

std::optional< MimeInfo > idhanScanMime( const std::byte* data, std::size_t len )
{}

} // namespace idhan::mime
