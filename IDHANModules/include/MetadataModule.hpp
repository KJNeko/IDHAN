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

struct MetadataInfo
{
	std::variant< std::monostate, MetadataInfoImage, MetadataInfoAnimation > m_metadata {};
	std::string m_extra {};
	SimpleMimeType m_simple_type { SimpleMimeType::NONE };
};

class FGL_EXPORT MetadataModuleI : public ModuleBase
{
  public:

	virtual ~MetadataModuleI() = default;

	virtual std::vector< std::string_view > handleableMimes() = 0;

	bool canHandle( const std::string_view mime )
	{
		for ( auto& handleable : handleableMimes() )
		{
			if ( handleable == mime ) return true;
		}

		return false;
	}

	virtual std::expected< MetadataInfo, ModuleError >
		parseFile( void* data, std::size_t length, std::string mime_name ) = 0;

	ModuleType type() override { return ModuleTypeFlags::METADATA; }
};
} // namespace idhan