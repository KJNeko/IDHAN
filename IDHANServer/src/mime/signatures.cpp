#include "signatures.hpp"

#include "MimeIDs.hpp"

namespace idhan::mime
{

using namespace std::string_view_literals;

// A hex escape swallows every hex digit that follows it, so a pattern whose escape is followed by
// one is written as two adjacent literals (see clip_head).

constexpr std::string_view png_magic[] { "\x89PNG\r\n\x1A\n"sv };
constexpr std::string_view actl_chunk[] { "acTL"sv };
//! IDAT, plus the 0x78 that opens its zlib stream.
constexpr std::string_view idat_chunk[] { "IDATx"sv };

constexpr Rule apng_rules[] {
	Rule { .offset = 0, .patterns = png_magic },
	Rule { .limit = 512, .patterns = actl_chunk },
	Rule { .limit = 512, .patterns = idat_chunk },
};

constexpr Rule png_rules[] { Rule { .offset = 0, .patterns = png_magic } };

constexpr std::string_view ftyp_box[] { "ftyp"sv };
constexpr std::string_view quicktime_brand[] { "qt  "sv };
constexpr std::string_view mp4_brands[] { "isom"sv, "mp41"sv, "mp42"sv, "mp43"sv, "mp4a"sv, "MSNV"sv };

constexpr Rule mov_rules[] {
	Rule { .offset = 4, .patterns = ftyp_box },
	Rule { .offset = 8, .patterns = quicktime_brand },
};

constexpr Rule mp4_rules[] {
	Rule { .offset = 4, .patterns = ftyp_box },
	Rule { .offset = 8, .patterns = mp4_brands },
};

constexpr std::string_view jpeg_magic[] { "\xFF\xD8\xFF"sv };
constexpr Rule jpeg_rules[] { Rule { .offset = 0, .patterns = jpeg_magic } };

constexpr std::string_view gif_magic[] { "GIF87a"sv, "GIF89a"sv };
constexpr Rule gif_rules[] { Rule { .offset = 0, .patterns = gif_magic } };

constexpr std::string_view riff_magic[] { "RIFF"sv };
constexpr std::string_view webp_form[] { "WEBP"sv };
constexpr Rule webp_rules[] {
	Rule { .offset = 0, .patterns = riff_magic },
	Rule { .offset = 8, .patterns = webp_form },
};

constexpr std::string_view webm_doctype[] { "webm"sv };
constexpr Rule webm_rules[] { Rule { .limit = 2048, .patterns = webm_doctype } };

constexpr std::string_view zip_magic[] { "PK\x03\x04"sv };
constexpr Rule zip_rules[] { Rule { .offset = 0, .patterns = zip_magic } };

constexpr std::string_view psd_magic[] { "8BPS"sv };
constexpr Rule psd_rules[] { Rule { .offset = 0, .patterns = psd_magic } };

constexpr std::string_view clip_magic[] { "CSFCHUNK"sv };
//! CHNKHead follows the 24 byte CSFCHUNK header; the limit keeps a corrupt file from being read whole.
constexpr std::string_view clip_head[] {
	"\x18"
	"CHNKHead"sv
};
constexpr Rule clip_rules[] {
	Rule { .offset = 0, .patterns = clip_magic },
	Rule { .limit = 256, .patterns = clip_head },
};

constexpr std::string_view tiff_magic[] { "II*\0"sv };
constexpr Rule tiff_rules[] { Rule { .offset = 0, .patterns = tiff_magic } };

constexpr Signature all_signatures[] {
	Signature { mime_ids::ANIMATION_APNG, apng_rules },   Signature { mime_ids::IMAGE_PNG, png_rules },
	Signature { mime_ids::VIDEO_QUICKTIME, mov_rules },   Signature { mime_ids::VIDEO_MP4, mp4_rules },
	Signature { mime_ids::IMAGE_JPEG, jpeg_rules },       Signature { mime_ids::ANIMATION_GIF, gif_rules },
	Signature { mime_ids::IMAGE_WEBP, webp_rules },       Signature { mime_ids::VIDEO_WEBM, webm_rules },
	Signature { mime_ids::APPLICATION_ZIP, zip_rules },   Signature { mime_ids::APPLICATION_PSD, psd_rules },
	Signature { mime_ids::APPLICATION_CLIP, clip_rules }, Signature { mime_ids::IMAGE_TIFF, tiff_rules },
};

std::span< const Signature > signatures()
{
	return all_signatures;
}

} // namespace idhan::mime
