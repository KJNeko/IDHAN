#pragma once

#include <json/json.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "PerceptualHash.hpp"

namespace idhan
{

//! Metadata blocks a file carries alongside its pixels.
struct EmbeddedMetadata
{
	bool exif { false };
	bool gps { false }; //!< An EXIF GPS directory, so the file names where it was taken.
	bool xmp { false };
	bool iptc { false };
	bool icc_profile { false };
};

enum class ChromaSubsampling : std::uint8_t
{
	UNKNOWN = 0,
	GREYSCALE = 1, //!< Single component; no chroma planes.
	NONE = 2, //!< 4:4:4
	HORIZONTAL = 3, //!< 4:2:2
	VERTICAL = 4, //!< 4:4:0
	BOTH = 5, //!< 4:2:0
};

struct EncodingQuality
{
	bool lossless { false };
	//! Estimated IJG quality (1-100) from JPEG quantization tables; absent when unavailable.
	std::optional< std::uint8_t > quality {};
	ChromaSubsampling subsampling { ChromaSubsampling::UNKNOWN };
	//! Absent when scan order is unavailable or unsupported by this parser.
	std::optional< bool > progressive {};
};

struct MetadataInfoImage
{
	int width { 0 };
	int height { 0 };
	std::uint8_t channels { 0 };
	std::optional< PerceptualHash > phash {};
	EmbeddedMetadata embedded {};
	//! Absent when encoding information is unavailable.
	std::optional< EncodingQuality > encoding {};
	//! Sanitized JPEG COM text. Empty if no text was retained; absent if comments were not parsed.
	std::optional< std::string > comment {};
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
