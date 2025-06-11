//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <expected>
#include <string>
#include <variant>
#include <vector>

#include "ModuleBase.hpp"

enum class MetadataInfoType
{
	IMAGE,
	ANIMATION,
	VIDEO,
	ARCHIVE,
	OTHER
};

struct MetadataInfoImage
{};

struct MetadataInfoAnimation
{};

struct MetadataInfo
{
	std::variant< MetadataInfoImage, MetadataInfoAnimation > m_metadata;
	std::string m_extra;
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

	virtual std::expected< MetadataInfo, ModuleError > parseImage( void* data, std::size_t length ) = 0;

	ModuleType type() override { return ModuleTypeFlags::METADATA; }
};