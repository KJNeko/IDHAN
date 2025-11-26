//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <array>
#include <expected>
#include <string>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "ModuleBase.hpp"

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

class FGL_EXPORT MetadataModuleI : public ModuleBase
{
  public:

	MetadataModuleI() = delete;

	MetadataModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~MetadataModuleI() override;

	virtual std::vector< std::string_view > handleableMimes() = 0;

	virtual std::expected< MetadataInfo, ModuleError > parseFile( ModuleCallData& data ) = 0;

	bool canHandle( std::string_view mime );

	ModuleType type() override;
};
} // namespace idhan