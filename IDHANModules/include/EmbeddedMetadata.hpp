#pragma once

#include "MetadataInfo.hpp"

struct _VipsImage;
using VipsImage = _VipsImage;

namespace idhan
{

//! Which metadata blocks \p image was decoded with. Reads the header only; no pixels are fetched.
[[nodiscard]] EmbeddedMetadata detectEmbeddedMetadata( VipsImage* image );

} // namespace idhan
