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
//! Bitmask flags declaring which interfaces a module implements. A module may combine several (e.g.
//! METADATA | THUMBNAILER); ModuleBase::type() returns the OR of the flags it supports.
enum ModuleTypeFlags : std::uint16_t
{
	METADATA = 1 << 0, //!< Implements MetadataModuleI::parseFile.
	THUMBNAILER = 1 << 1, //!< Implements ThumbnailerModuleI::createThumbnail.
	GENERATOR = 1 << 2, //!< Implements GeneratorModuleI::generate.
};

//! Bitwise-OR of ModuleTypeFlags values; the concrete return type of ModuleBase::type().
using ModuleType = std::uint16_t;

//! Semantic version of a module, reported by ModuleBase::version().
struct ModuleVersion
{
	std::size_t m_major { 1 };
	std::size_t m_minor { 0 };
	std::size_t m_patch { 0 };
};

//! Non-owning view over raw file bytes handed to a module.
using data_view = std::basic_string_view< std::uint8_t >;

//! Human-readable error message returned (via std::expected) when a module operation fails.
using ModuleError = std::string;

//! Input passed to a module for a single operation.
struct ModuleCallData
{
	idhan::data_view file_view; //!< The file's raw bytes; not owned, valid only for the call's duration.
	std::string mime_name; //!< Canonical MIME type of the file, as resolved by the mime database.
	Json::Value extra; //!< Optional caller-supplied parameters; contents are operation-specific.
};

//! Host callbacks handed to every module at construction so it can re-dispatch work back through the
//! module system — e.g. an archive thumbnailer asking the host to thumbnail a contained file. The
//! host resolves the target module by MIME. These run synchronously and may re-enter the calling
//! module (see ModuleBase::threadSafe).
struct FGL_EXPORT ModuleCallbacks
{
	using ThumbnailFunc = std::function<
		std::expected< ThumbnailInfo, ModuleError >( const std::vector< std::byte >&, Json::Value, std::string ) >;
	using GenerateFunc = std::function< std::expected<
		std::vector< std::byte >,
		ModuleError >( data_view, std::array< std::byte, 256 / 8 >, Json::Value, std::string ) >;

	ThumbnailFunc thumbnail; //!< Ask the host to thumbnail the given bytes (data, extra, file_name).
	GenerateFunc generate; //!< Ask the host to generate a derived file matching the desired hash.

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

	//! Host callbacks for re-dispatching work through the module system (see ModuleCallbacks).
	ModuleCallbacks m_callbacks;

	//! Short human-readable name of the module, used in logs.
	[[nodiscard]] virtual std::string_view name() = 0;

	ModuleBase() = delete;

	ModuleBase( ModuleCallbacks callbacks ) : m_callbacks( callbacks ) {}

	virtual ~ModuleBase() = default;

	// Reports whether concurrent calls into this module are safe. Default is false (assume unsafe) so
	// a module must explicitly opt in. This is the hook for a planned dispatch feature: modules that
	// return false will be serialized — never run on more than one thread at once — so thread-hostile
	// modules don't error. It is intentionally unused for now; do not remove it.
	//
	// A future serializer must key the lock per-module and use a recursive lock: the thumbnail/generate
	// callbacks re-dispatch through ModuleLoader synchronously and can re-enter the SAME module on the
	// SAME thread (e.g. nested archives). Any module that recurses into itself must therefore report
	// true, as the premade Archive modules do, so they are never serialized.
	[[nodiscard]] virtual bool threadSafe() { return false; }

	//! The interfaces this module implements, as an OR of ModuleTypeFlags.
	[[nodiscard]] virtual ModuleType type() = 0;

	//! The module's semantic version.
	[[nodiscard]] virtual ModuleVersion version() = 0;
};

using IDHANModule = ModuleBase;

} // namespace idhan
