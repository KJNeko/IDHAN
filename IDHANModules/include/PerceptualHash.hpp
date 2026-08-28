#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

#include "IDHANTypes.hpp"
#include "ModuleCommon.hpp"

struct _VipsImage;
using VipsImage = _VipsImage;

namespace idhan
{

using PerceptualHash = std::array< std::byte, 8 >;

[[nodiscard]] bool isPerceptualHashMime( MimeID mime_id );

//! The number of differing bits between two 64-bit perceptual hashes, in the inclusive range 0-64.
[[nodiscard]] std::uint8_t perceptualHashDistance( const PerceptualHash& left, const PerceptualHash& right );

//! Generates Hydrus's 64-bit shape pHash. A successful empty result is a hash rejected as blank.
[[nodiscard]] std::expected< std::optional< PerceptualHash >, ModuleError > generatePerceptualHash( VipsImage* image );

} // namespace idhan
