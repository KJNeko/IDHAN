#pragma once
#ifndef MRMIME_FILETYPE_STRINGS_HPP_INCLUDED
#define MRMIME_FILETYPE_STRINGS_HPP_INCLUDED

#include <string>
#include "filetype_enum.h"

namespace MrMime {

[[nodiscard]] constexpr const char* fileType_to_cstr(const FileType filetype)
{
	switch(filetype)
	{
	case IMAGE_JPEG: return {"IMAGE_JPEG"};
	case IMAGE_PNG: return {"IMAGE_PNG"};
	case IMAGE_GIF: return {"IMAGE_GIF"};
	case IMAGE_BMP: return {"IMAGE_BMP"};
	case APPLICATION_FLASH: return {"APPLICATION_FLASH"};
//	case APPLICATION_YAML: return {"APPLICATION_YAML"};
	case IMAGE_ICON: return {"IMAGE_ICON"};
//	case TEXT_HTML: return {"TEXT_HTML"};
	case VIDEO_FLV: return {"VIDEO_FLV"};
	case APPLICATION_PDF: return {"APPLICATION_PDF"};
	case APPLICATION_ZIP: return {"APPLICATION_ZIP"};
	case APPLICATION_HYDRUS_ENCRYPTED_ZIP: return {"APPLICATION_HYDRUS_ENCRYPTED_ZIP"};
//	case AUDIO_MP3: return {"AUDIO_MP3"};
	case VIDEO_MP4: return {"VIDEO_MP4"};
//	case AUDIO_OGG: return {"AUDIO_OGG"};
	case AUDIO_FLAC: return {"AUDIO_FLAC"};
//	case AUDIO_WMA: return {"AUDIO_WMA"};
//	case VIDEO_WMV: return {"VIDEO_WMV"};
	case UNDETERMINED_WM: return {"UNDETERMINED_WM"};
//	case VIDEO_MKV: return {"VIDEO_MKV"};
//	case VIDEO_WEBM: return {"VIDEO_WEBM"};
//	case APPLICATION_JSON: return {"APPLICATION_JSON"};
	case IMAGE_APNG: return {"IMAGE_APNG"};
//	case UNDETERMINED_PNG: return {"UNDETERMINED_PNG"};
//	case VIDEO_MPEG: return {"VIDEO_MPEG"};
	case VIDEO_MOV: return {"VIDEO_MOV"};
	case VIDEO_AVI: return {"VIDEO_AVI"};
//	case APPLICATION_HYDRUS_UPDATE_DEFINITIONS: return {"APPLICATION_HYDRUS_UPDATE_DEFINITIONS"};
//	case APPLICATION_HYDRUS_UPDATE_CONTENT: return {"APPLICATION_HYDRUS_UPDATE_CONTENT"};
//	case TEXT_PLAIN: return {"TEXT_PLAIN"};
	case APPLICATION_RAR: return {"APPLICATION_RAR"};
	case APPLICATION_7Z: return {"APPLICATION_7Z"};
	case IMAGE_WEBP: return {"IMAGE_WEBP"};
	case IMAGE_TIFF: return {"IMAGE_TIFF"};
	case APPLICATION_PSD: return {"APPLICATION_PSD"};
//	case AUDIO_M4A: return {"AUDIO_M4A"};
//	case VIDEO_REALMEDIA: return {"VIDEO_REALMEDIA"};
//	case AUDIO_REALMEDIA: return {"AUDIO_REALMEDIA"};
//	case AUDIO_TRUEAUDIO: return {"AUDIO_TRUEAUDIO"};
//	case GENERAL_AUDIO: return {"GENERAL_AUDIO"};
//	case GENERAL_IMAGE: return {"GENERAL_IMAGE"};
//	case GENERAL_VIDEO: return {"GENERAL_VIDEO"};
//	case GENERAL_APPLICATION: return {"GENERAL_APPLICATION"};
//	case GENERAL_ANIMATION: return {"GENERAL_ANIMATION"};
	case APPLICATION_CLIP: return {"APPLICATION_CLIP"};
	case AUDIO_WAVE: return {"AUDIO_WAVE"};
//	case APPLICATION_OCTET_STREAM: return {"APPLICATION_OCTET_STREAM"};
	case APPLICATION_UNKNOWN: return {"APPLICATION_UNKNOWN"};
	default: return {"Undefined FileType String"};
	};
}

[[nodiscard]] std::string fileType_to_string(const FileType filetype)
{
	return std::string(fileType_to_cstr(filetype));
}

} // namespace MrMime

#endif // MRMIME_FILETYPE_STRINGS_HPP_INCLUDED
