#pragma once

#include <json/json.h>

#include <cstdint>

#include "IDHANTypes.hpp"

namespace idhan
{

//! Metadata for a still image.
struct MetadataInfoImage
{
	int width { 0 };
	int height { 0 };
	std::uint8_t channels { 0 }; //!< Number of colour/alpha channels.
};

//! Metadata for an animation. Currently carries no fields beyond its type tag.
struct MetadataInfoAnimation
{};

//! Metadata for a layered image project (PSD, XCF, etc.).
struct MetadataInfoImageProject
{
	MetadataInfoImage image_info {}; //!< Dimensions of the flattened canvas.
	std::uint8_t layers { 0 }; //!< Number of layers.
};

//! Metadata for a video (or audio-bearing container).
struct MetadataInfoVideo
{
	bool m_has_audio { false };
	int m_width { 0 };
	int m_height { 0 };
	int m_bitrate { 0 }; //!< Overall bitrate in bits per second.
	double m_duration { 0.0 }; //!< Duration in seconds.
	double m_fps { 0.0 }; //!< Frames per second.
};

//! Metadata for an archive (zip, rar, etc.).
struct MetadataInfoArchive
{
	//! Hashes of all contained files
	std::vector< std::array< std::byte, ( 256 / 8 ) > > contained_hashes {};
	//! The size of all files when decompressed
	std::size_t m_size { 0 };
	bool encrypted { false }; //!< True if the archive's contents are encrypted.
};

//! The per-category metadata payload; std::monostate means "no specific metadata".
using MetadataVariant = std::variant<
	std::monostate,
	MetadataInfoImage,
	MetadataInfoVideo,
	MetadataInfoImageProject,
	MetadataInfoAnimation,
	MetadataInfoArchive >;

//! Result of MetadataModuleI::parseFile: the typed metadata plus a coarse category and extra JSON.
struct MetadataInfo
{
	MetadataVariant m_metadata {}; //!< Category-specific fields (see MetadataVariant).

	Json::Value m_extra {}; //!< Free-form extra metadata the module chose to surface.
	SimpleMimeType m_simple_type { SimpleMimeType::NONE }; //!< Coarse media category of the file.
};

} // namespace idhan