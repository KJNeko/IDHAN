#pragma once

#include <array>
#include <string_view>
#include <unordered_map>

#include "IDHANTypes.hpp"

//! The closed set of mime ids IDHAN recognises.
//!
//! Ids are allocated in blocks of 1000, one per SimpleMimeType category. A file's mime is resolved in
//! two stages: a pre-scan settles the base type, then a parser registered for that base may refine it
//! into a more specific id. A refined id reports the same mime string as its base, so this map is
//! many-to-one and cannot be inverted.
namespace idhan::mime_ids
{

//! Not a mime. The absence of one.
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

//! Reserved. table_audio_metadata is still in progress and will fill this block.
constexpr MimeID AUDIO_BLOCK { 4000 };

constexpr MimeID ARCHIVE_BLOCK { 5000 };
constexpr MimeID APPLICATION_ZIP { 5000 };
constexpr MimeID COMICBOOK_ZIP { 5001 };
//! A zip carrying an animation.json manifest. Reports "application/zip".
constexpr MimeID PIXIV_UGOIRA { 5002 };

constexpr MimeID IMAGE_PROJECT_BLOCK { 6000 };
constexpr MimeID APPLICATION_PSD { 6000 };
constexpr MimeID APPLICATION_CLIP { 6001 };

//! The first id the mime table's sequence may hand out, above every reserved block.
constexpr MimeID FIRST_UNRESERVED { 10000 };

//! Every declared mime id. INVALID is excluded: it names the absence of a mime.
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

//! Mime id to the string it reports. Many-to-one: a refined id repeats its base's string.
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
	{ COMICBOOK_ZIP, "application/vnd.comicbook+zip" },
	{ PIXIV_UGOIRA, "application/zip" },
	{ APPLICATION_PSD, "application/psd" },
	{ APPLICATION_CLIP, "application/x-clip-studio" },
};

//! Best file extension per mime id. Seeded into mime.best_extension, which is NOT NULL.
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

//! The id a bare mime string resolves to, which is the lowest id declared with that name. Ids
//! sharing a name are ordered generic first, so a plain "application/zip" never resolves to the
//! Ugoira that also reports it. INVALID when no id carries \p name.
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
