#pragma once

#include <array>
#include <string_view>
#include <unordered_map>

#include "IDHANTypes.hpp"

//! IDs use blocks of 1000 per SimpleMimeType. A pre-scan identifies the base type; a parser may
//! refine it. Refined IDs share their base's MIME string, so the mapping cannot be inverted.
namespace idhan::mime_ids
{

//! No MIME assigned.
constexpr MimeID INVALID { 0 };

//! A file whose bytes matched no identifier.
constexpr MimeID UNKNOWN { 1 };

constexpr MimeID IMAGE_BLOCK { 1000 };
constexpr MimeID IMAGE_JPEG { 1000 };
constexpr MimeID IMAGE_PNG { 1001 };
constexpr MimeID IMAGE_WEBP { 1002 };
constexpr MimeID IMAGE_AVIF { 1003 };
constexpr MimeID IMAGE_TIFF { 1004 };

constexpr MimeID VIDEO_BLOCK { 2000 };
constexpr MimeID VIDEO_MP4 { 2000 };
constexpr MimeID VIDEO_MPEG { 2001 };
constexpr MimeID VIDEO_WEBM { 2002 };
constexpr MimeID VIDEO_QUICKTIME { 2003 };

constexpr MimeID ANIMATION_BLOCK { 3000 };
constexpr MimeID ANIMATION_GIF { 3000 };
constexpr MimeID ANIMATION_APNG { 3001 };

//! Reserved for table_audio_metadata.
constexpr MimeID AUDIO_BLOCK { 4000 };

constexpr MimeID ARCHIVE_BLOCK { 5000 };
constexpr MimeID APPLICATION_ZIP { 5000 };
constexpr MimeID COMICBOOK_ZIP { 5001 };
//! A zip carrying an animation.json manifest, or holding nothing but a numbered image sequence at
//! its root. Reports "application/zip".
constexpr MimeID PIXIV_UGOIRA { 5002 };

constexpr MimeID IMAGE_PROJECT_BLOCK { 6000 };
constexpr MimeID APPLICATION_PSD { 6000 };
constexpr MimeID APPLICATION_CLIP { 6001 };

//! The first id the mime table's sequence may hand out, above every reserved block.
constexpr MimeID FIRST_UNRESERVED { 10000 };

[[nodiscard]] constexpr SimpleMimeType simpleType( const MimeID mime_id )
{
	if ( mime_id < IMAGE_BLOCK || mime_id >= FIRST_UNRESERVED ) return SimpleMimeType::NONE;

	return static_cast< SimpleMimeType >( mime_id / 1000 );
}

//! Excludes INVALID, which represents an absent MIME.
inline constexpr std::array< MimeID, 17 > ALL_MIME_IDS {
	{ UNKNOWN,
	  IMAGE_JPEG,
	  IMAGE_PNG,
	  IMAGE_WEBP,
	  IMAGE_AVIF,
	  IMAGE_TIFF,
	  VIDEO_MP4,
	  VIDEO_MPEG,
	  VIDEO_WEBM,
	  VIDEO_QUICKTIME,
	  ANIMATION_GIF,
	  ANIMATION_APNG,
	  APPLICATION_ZIP,
	  COMICBOOK_ZIP,
	  PIXIV_UGOIRA,
	  APPLICATION_PSD,
	  APPLICATION_CLIP }
};

//! Multiple IDs can share a MIME string.
inline const std::unordered_map< MimeID, std::string_view > mime_names {
	{ UNKNOWN, "unknown/unknown" },
	{ IMAGE_JPEG, "image/jpeg" },
	{ IMAGE_PNG, "image/png" },
	{ IMAGE_WEBP, "image/webp" },
	{ IMAGE_AVIF, "image/avif" },
	{ IMAGE_TIFF, "image/tiff" },
	{ VIDEO_MP4, "video/mp4" },
	{ VIDEO_MPEG, "video/mpeg" },
	{ VIDEO_WEBM, "video/webm" },
	{ VIDEO_QUICKTIME, "video/quicktime" },
	{ ANIMATION_GIF, "image/gif" },
	{ ANIMATION_APNG, "image/apng" },
	{ APPLICATION_ZIP, "application/zip" },
	{ COMICBOOK_ZIP, "application/zip" },
	{ PIXIV_UGOIRA, "application/zip" },
	{ APPLICATION_PSD, "application/psd" },
	{ APPLICATION_CLIP, "application/x-clip-studio" },
};

//! Seeds the NOT NULL mime.best_extension column.
inline const std::unordered_map< MimeID, std::string_view > mime_extensions {
	{ UNKNOWN, "" },
	{ IMAGE_JPEG, "jpg" },
	{ IMAGE_PNG, "png" },
	{ IMAGE_WEBP, "webp" },
	{ IMAGE_AVIF, "avif" },
	{ IMAGE_TIFF, "tiff" },
	{ VIDEO_MP4, "mp4" },
	{ VIDEO_MPEG, "mpeg" },
	{ VIDEO_WEBM, "webm" },
	{ VIDEO_QUICKTIME, "mov" },
	{ ANIMATION_GIF, "gif" },
	{ ANIMATION_APNG, "png" },
	{ APPLICATION_ZIP, "zip" },
	{ COMICBOOK_ZIP, "cbz" },
	{ PIXIV_UGOIRA, "zip" },
	{ APPLICATION_PSD, "psd" },
	{ APPLICATION_CLIP, "clip" },
};

//! Returns the lowest matching ID (generic before refined), or INVALID if unknown.
//! For example, "application/zip" resolves to APPLICATION_ZIP rather than PIXIV_UGOIRA.
[[nodiscard]] inline MimeID canonicalIDForName( const std::string_view name )
{
	MimeID canonical { INVALID };

	for ( const auto id : ALL_MIME_IDS )
	{
		if ( mime_names.at( id ) != name ) continue;
		if ( canonical == INVALID || id < canonical ) canonical = id;
	}

	return canonical;
}

} // namespace idhan::mime_ids
