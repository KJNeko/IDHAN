#pragma once
#ifndef MRMIME_FILETYPE_ENUM_H_INCLUDED
#define MRMIME_FILETYPE_ENUM_H_INCLUDED

#include <cstdint> // uint8_t

namespace MrMime {

/*
	Conforming to HydrusConstants.py (based on version 455)
	https://github.com/hydrusnetwork/hydrus/blob/master/hydrus/core/HydrusConstants.py

	Don't return enum to hydrus directly!
	Use `MrMime::hydrus_compatible_filetype(FileType)` for future compatibility

	Commented out unused constants.
*/

enum FileType : uint8_t
{
	IMAGE_JPEG = 1,
	IMAGE_PNG = 2,
	IMAGE_GIF = 3,
	IMAGE_BMP = 4,
	APPLICATION_FLASH = 5,
//	APPLICATION_YAML = 6,
	IMAGE_ICON = 7,
//	TEXT_HTML = 8,
	VIDEO_FLV = 9,
	APPLICATION_PDF = 10,
	APPLICATION_ZIP = 11,
	APPLICATION_HYDRUS_ENCRYPTED_ZIP = 12,
//	AUDIO_MP3 = 13,
	VIDEO_MP4 = 14,
//	AUDIO_OGG = 15,
	AUDIO_FLAC = 16,
//	AUDIO_WMA = 17,
//	VIDEO_WMV = 18,
	UNDETERMINED_WM = 19,
//	VIDEO_MKV = 20,
//	VIDEO_WEBM = 21,
//	APPLICATION_JSON = 22,
	IMAGE_APNG = 23,
//	UNDETERMINED_PNG = 24,
//	VIDEO_MPEG = 25,
	VIDEO_MOV = 26,
	VIDEO_AVI = 27,
//	APPLICATION_HYDRUS_UPDATE_DEFINITIONS = 28,
//	APPLICATION_HYDRUS_UPDATE_CONTENT = 29,
//	TEXT_PLAIN = 30,
	APPLICATION_RAR = 31,
	APPLICATION_7Z = 32,
	IMAGE_WEBP = 33,
	IMAGE_TIFF = 34,
	APPLICATION_PSD = 35,
//	AUDIO_M4A = 36,
//	VIDEO_REALMEDIA = 37,
//	AUDIO_REALMEDIA = 38,
//	AUDIO_TRUEAUDIO = 39,
//	GENERAL_AUDIO = 40,
//	GENERAL_IMAGE = 41,
//	GENERAL_VIDEO = 42,
//	GENERAL_APPLICATION = 43,
//	GENERAL_ANIMATION = 44,
	APPLICATION_CLIP = 45,
	AUDIO_WAVE = 46,
//	APPLICATION_OCTET_STREAM = 100,
	APPLICATION_UNKNOWN = 101,
};

/// Returns a FileType signed integer constant that's compatible with Hydrus
// Don't return FileType enum directly! Use this to ensure compatibility.
[[nodiscard]] inline int hydrus_compatible_filetype(const FileType ft)
{ return static_cast<int>(ft); }

} // namespace MrMime

#endif // MRMIME_FILETYPE_ENUM_H_INCLUDED
