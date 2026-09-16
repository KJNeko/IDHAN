#pragma once
#include <optional>
#include <variant>

#include "ExtensionDocument.hpp"

namespace idhan::ipc::binary
{
struct EmbeddedMetadata
{
	bool exif, gps, xmp, iptc, icc_profile;
};

struct EncodingQuality
{
	bool lossless;
	std::optional< std::uint32_t > quality;
	std::uint32_t subsampling;
	std::optional< bool > progressive;
};

struct ImageMetadata
{
	std::int32_t width, height;
	std::uint32_t channels;
	std::optional< std::array< std::byte, 8 > > phash;
	EmbeddedMetadata embedded;
	std::optional< EncodingQuality > encoding;
	std::optional< std::string > comment;
};

struct VideoMetadata
{
	bool has_audio;
	std::int32_t width, height, bitrate_bps;
	double duration_s, fps;
};

struct ImageProjectMetadata
{
	ImageMetadata image;
	std::uint32_t layers;
};

struct AnimationMetadata
{
	std::int32_t width, height, frame_count;
	double duration_s;
	bool loops;
};

struct ArchiveEntry
{
	module::SHA256 hash;
	std::string path;
};

struct ArchiveMetadata
{
	std::vector< ArchiveEntry > records;
	std::uint64_t uncompressed_size;
	bool encrypted;
};

struct AudioMetadata
{
	double duration_s;
	std::int32_t bitrate_bps;
	std::uint32_t channels;
	std::int32_t sample_rate;
};

using MetadataValue = std::variant<
	std::monostate,
	ImageMetadata,
	VideoMetadata,
	ImageProjectMetadata,
	AnimationMetadata,
	ArchiveMetadata,
	AudioMetadata >;

struct MetadataInfo
{
	SimpleMimeType simple_type;
	MetadataValue value;
	std::optional< ExtensionDocument > extra;
};
} // namespace idhan::ipc::binary
