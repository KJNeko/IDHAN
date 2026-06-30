//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <json/value.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <string>

#include "ThumbnailInfo.hpp"

#ifdef _WIN32
#define FGL_EXPORT __declspec( dllexport )
#else
#define FGL_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

namespace idhan
{
enum ModuleTypeFlags : std::uint16_t
{
	METADATA = 1 << 0,
	THUMBNAILER = 1 << 1,
	GENERATOR = 1 << 2,
};

using ModuleType = std::uint16_t;

struct ModuleVersion
{
	std::size_t m_major { 1 };
	std::size_t m_minor { 0 };
	std::size_t m_patch { 0 };
};

using data_view = std::basic_string_view< std::uint8_t >;

using ModuleError = std::string;

struct ModuleCallData
{
	idhan::data_view file_view;
	std::string mime_name;
	Json::Value extra;
};

struct FGL_EXPORT ModuleCallbacks
{
	using ThumbnailFunc = std::function<
		std::expected< ThumbnailInfo, ModuleError >( const std::vector< std::byte >&, Json::Value, std::string ) >;
	using GenerateFunc = std::function< std::expected<
		std::vector< std::byte >,
		ModuleError >( data_view, std::array< std::byte, 256 / 8 >, Json::Value, std::string ) >;

	ThumbnailFunc thumbnail;
	GenerateFunc generate;

	/*
	//! Generates a thumbnail for the given file. Returns it in a RGB format
	virtual std::expected< ThumbnailInfo, ModuleError > thumbnail(
		const std::vector< std::byte >& data,
		Json::Value extra = {},
		std::string file_name = "" ) = 0;

	virtual std::expected< std::vector< std::byte >, ModuleError > generate(
		data_view data,
		std::array< std::byte, 256 / 8 > hash,
		Json::Value extra = {},
		std::string file_name = "" ) = 0;
	*/
};

class FGL_EXPORT ModuleBase
{
  public:

	ModuleCallbacks m_callbacks;

	[[nodiscard]] virtual std::string_view name() = 0;

	ModuleBase() = delete;

	ModuleBase( ModuleCallbacks callbacks ) : m_callbacks( callbacks ) {}

	virtual ~ModuleBase() = default;

	[[nodiscard]] virtual bool threadSafe() { return false; }

	[[nodiscard]] virtual ModuleType type() = 0;

	[[nodiscard]] virtual ModuleVersion version() = 0;
};

using IDHANModule = ModuleBase;

} // namespace idhan
