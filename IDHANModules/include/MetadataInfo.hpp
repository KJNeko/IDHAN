#pragma once

#include <json/json.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan
{

struct MetadataInfoImage
{
	int width { 0 };
	int height { 0 };
	std::uint8_t channels { 0 };
};

struct MetadataInfoAnimation
{
	int width { 0 };
	int height { 0 };
	int frame_count { 0 };
	double duration_s { 0.0 };
	bool loops { false };
};

struct MetadataInfoAudio
{
	double duration_s { 0.0 };
	int bitrate_bps { 0 };
	std::uint8_t channels { 0 };
	int sample_rate { 0 };
};

//! PSD, XCF, and the like.
struct MetadataInfoImageProject
{
	MetadataInfoImage image_info {}; //!< Dimensions of the flattened canvas.
	std::uint8_t layers { 0 };
};

struct MetadataInfoVideo
{
	bool has_audio { false };
	int width { 0 };
	int height { 0 };
	int bitrate_bps { 0 };
	double duration_s { 0.0 };
	double fps { 0.0 };
};

struct ArchiveEntry
{
	std::array< std::byte, ( 256 / 8 ) > hash {};
	std::string path {};
};

struct MetadataInfoArchive
{
	std::vector< ArchiveEntry > contained_records {};
	std::size_t m_size { 0 };
	bool encrypted { false };
};

using MetadataVariant = std::variant<
	std::monostate,
	MetadataInfoImage,
	MetadataInfoVideo,
	MetadataInfoImageProject,
	MetadataInfoAnimation,
	MetadataInfoArchive,
	MetadataInfoAudio >;

//! Result of MetadataModuleI::parseFile.
struct MetadataInfo
{
	MetadataVariant m_metadata {};

	Json::Value m_extra {};
	SimpleMimeType m_simple_type { SimpleMimeType::NONE };
};

} // namespace idhan