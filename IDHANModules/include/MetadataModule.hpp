//
// Created by kj16609 on 6/11/25.
//
#pragma once
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
	int width;
	int height;
	std::uint8_t channels;
};

struct MetadataInfoAnimation
{};

struct MetadataInfoImageProject
{
	MetadataInfoImage image_info;
	std::uint8_t layers;
};

struct MetadataInfo
{
	std::variant< std::monostate, MetadataInfoImageProject, MetadataInfoImage, MetadataInfoAnimation > m_metadata {};
	std::string m_extra {};
	SimpleMimeType m_simple_type { SimpleMimeType::NONE };
};

class FGL_EXPORT MetadataModuleI : public ModuleBase
{
  public:

	MetadataModuleI();

	~MetadataModuleI() override;

	virtual std::vector< std::string_view > handleableMimes() = 0;

	virtual std::expected< MetadataInfo, ModuleError > parseFile(
		const void* data,
		std::size_t length,
		std::string mime_name ) = 0;

	bool canHandle( std::string_view mime );

	ModuleType type() override;
};
} // namespace idhan