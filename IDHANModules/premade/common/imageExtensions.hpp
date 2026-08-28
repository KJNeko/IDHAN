#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace idhan::premade
{

// gif and apng are absent: an animated frame would make a zip a sequence of animations rather than
// the frames of one.
constexpr std::array< std::string_view, 7 > FRAME_EXTENSIONS { "jpg", "jpeg", "png", "webp", "avif", "tif", "tiff" };

[[nodiscard]] inline bool isFrameExtension( const std::string_view extension )
{
	std::string lowered { extension };
	std::ranges::transform(
		lowered,
		lowered.begin(),
		[]( const char c ) { return static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) ); } );

	return std::ranges::contains( FRAME_EXTENSIONS, lowered );
}

[[nodiscard]] inline bool hasFrameExtension( const std::string_view path )
{
	const auto dot { path.find_last_of( '.' ) };

	return dot != std::string_view::npos && isFrameExtension( path.substr( dot + 1 ) );
}

} // namespace idhan::premade
