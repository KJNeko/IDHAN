//
// Created by kj16609 on 11/28/25.
//
#pragma once

#include <json/json.h>

#include <cstdint>

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
{};

struct MetadataInfoImageProject
{
	MetadataInfoImage image_info {};
	std::uint8_t layers { 0 };
};

struct MetadataInfoVideo
{
	bool m_has_audio { false };
	int m_width { 0 };
	int m_height { 0 };
	int m_bitrate { 0 };
	double m_duration { 0.0 };
	double m_fps { 0.0 };
};

struct MetadataInfoArchive
{
	//! Hashes of all contained files
	std::vector< std::array< std::byte, ( 256 / 8 ) > > contained_hashes {};
	//! The size of all files when decompressed
	std::size_t m_size { 0 };
	bool encrypted { false };
};

using MetadataVariant = std::variant<
	std::monostate,
	MetadataInfoImage,
	MetadataInfoVideo,
	MetadataInfoImageProject,
	MetadataInfoAnimation,
	MetadataInfoArchive >;

struct MetadataInfo
{
	MetadataVariant m_metadata {};

	Json::Value m_extra {};
	SimpleMimeType m_simple_type { SimpleMimeType::NONE };
};

} // namespace idhan