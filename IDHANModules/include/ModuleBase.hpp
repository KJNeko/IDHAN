//
// Created by kj16609 on 6/11/25.
//
#pragma once

#include <cstdint>
#define FGL_EXPORT __attribute__( ( visibility( "default" ) ) )

namespace idhan
{
enum ModuleTypeFlags : std::uint16_t
{
	METADATA = 1 << 0,
	THUMBNAILER = 1 << 1,
};

using ModuleType = std::uint16_t;

class FGL_EXPORT ModuleBase
{
  public:

	virtual std::string_view name() = 0;

	virtual ~ModuleBase() = default;

	virtual bool threadSafe() { return false; }

	virtual ModuleType type() = 0;
};

struct ModuleInfo
{
	std::string_view m_name { "IDHAN Premade Module" };
	std::size_t m_version { 1 };
};

using IDHANModule = ModuleBase;

using ModuleError = std::string;
} // namespace idhan