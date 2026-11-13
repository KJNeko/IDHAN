#pragma once

#include <array>
#include <meta>
#include <string_view>
#include <unordered_map>

#include "IDHANTypes.hpp"

namespace idhan::mime_ids
{

struct MimeMetadata
{
	const char* name;
	const char* extension;

	consteval MimeMetadata( std::string_view mime_name, std::string_view best_extension ) :
	  name { std::define_static_string( mime_name ) },
	  extension { std::define_static_string( best_extension ) }
	{}
};

enum KnownMimeID : MimeID
{
	INVALID = 0,
	UNKNOWN[ [= MimeMetadata { "unknown/unknown", "" }] ] = 1,
	IMAGE_JPEG[ [= MimeMetadata { "image/jpeg", "jpg" }] ] = 1000,
	IMAGE_PNG[ [= MimeMetadata { "image/png", "png" }] ] = 1001,
	IMAGE_WEBP[ [= MimeMetadata { "image/webp", "webp" }] ] = 1002,
	IMAGE_AVIF[ [= MimeMetadata { "image/avif", "avif" }] ] = 1003,
	IMAGE_TIFF[ [= MimeMetadata { "image/tiff", "tiff" }] ] = 1004,
	VIDEO_MP4[ [= MimeMetadata { "video/mp4", "mp4" }] ] = 2000,
	VIDEO_MPEG[ [= MimeMetadata { "video/mpeg", "mpeg" }] ] = 2001,
	VIDEO_WEBM[ [= MimeMetadata { "video/webm", "webm" }] ] = 2002,
	VIDEO_QUICKTIME[ [= MimeMetadata { "video/quicktime", "mov" }] ] = 2003,
	ANIMATION_GIF[ [= MimeMetadata { "image/gif", "gif" }] ] = 3000,
	ANIMATION_APNG[ [= MimeMetadata { "image/apng", "png" }] ] = 3001,
	APPLICATION_ZIP[ [= MimeMetadata { "application/zip", "zip" }] ] = 5000,
	COMICBOOK_ZIP[ [= MimeMetadata { "application/zip", "cbz" }] ] = 5001,
	PIXIV_UGOIRA[ [= MimeMetadata { "application/zip", "zip" }] ] = 5002,
	APPLICATION_PSD[ [= MimeMetadata { "application/psd", "psd" }] ] = 6000,
	APPLICATION_CLIP[ [= MimeMetadata { "application/x-clip-studio", "clip" }] ] = 6001,
};

inline constexpr MimeID IMAGE_BLOCK { 1000 };
inline constexpr MimeID VIDEO_BLOCK { 2000 };
inline constexpr MimeID ANIMATION_BLOCK { 3000 };
inline constexpr MimeID AUDIO_BLOCK { 4000 };
inline constexpr MimeID ARCHIVE_BLOCK { 5000 };
inline constexpr MimeID IMAGE_PROJECT_BLOCK { 6000 };
inline constexpr MimeID FIRST_UNRESERVED { 10000 };

[[nodiscard]] constexpr SimpleMimeType simpleType( const MimeID mime_id )
{
	if ( mime_id < IMAGE_BLOCK || mime_id >= FIRST_UNRESERVED ) return SimpleMimeType::NONE;

	return static_cast< SimpleMimeType >( mime_id / 1000 );
}

consteval auto allMimeIDs()
{
	std::array< MimeID, std::meta::enumerators_of( ^^KnownMimeID ).size() - 1 > ids {};
	std::size_t index { 0 };
	template for ( constexpr auto enumerator : std::define_static_array( std::meta::enumerators_of( ^^KnownMimeID ) ) )
	{
		if constexpr ( [:enumerator:] != INVALID ) ids[ index++ ] = [:enumerator:];
	}

	return ids;
}

inline constexpr auto ALL_MIME_IDS { allMimeIDs() };

template < bool Extensions >
inline auto mimeMetadataMap()
{
	std::unordered_map< MimeID, std::string_view > values;
	template for ( constexpr auto enumerator : std::define_static_array( std::meta::enumerators_of( ^^KnownMimeID ) ) )
	{
		if constexpr ( [:enumerator:] != INVALID )
		{
			constexpr auto annotations {
				std::define_static_array( std::meta::annotations_of_with_type( enumerator, ^^MimeMetadata ) )
			};
			static_assert( annotations.size() == 1, "Each MIME ID needs a name and extension" );
			constexpr auto metadata { std::meta::extract< MimeMetadata >( annotations[ 0 ] ) };
			values.emplace( [:enumerator:], Extensions ? metadata.extension : metadata.name );
		}
	}

	return values;
}

inline const auto mime_names { mimeMetadataMap< false >() };

inline const auto mime_extensions { mimeMetadataMap< true >() };

} // namespace idhan::mime_ids
