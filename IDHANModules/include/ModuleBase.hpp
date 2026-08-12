#pragma once
#include <json/value.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "ModuleCommon.hpp"
#include "ModuleFile.hpp"
#include "ThumbnailInfo.hpp"

namespace idhan
{
//! Bitmask flags declaring which interfaces a module implements. A module may combine several (e.g.
//! METADATA | THUMBNAILER); ModuleBase::type() returns the OR of the flags it supports.
enum ModuleTypeFlags : std::uint16_t
{
	METADATA = 1 << 0, //!< Implements MetadataModuleI::parseFile.
	THUMBNAILER = 1 << 1, //!< Implements ThumbnailerModuleI::createThumbnail.
	GENERATOR = 1 << 2, //!< Implements GeneratorModuleI::generate.
	EMBEDDING = 1 << 3, //!< Implements EmbeddingModuleI::embed.
};

//! Bitwise-OR of ModuleTypeFlags values; the concrete return type of ModuleBase::type().
using ModuleType = std::uint16_t;

//! How the host runs a module, as reported by ModuleBase::residency().
//!
//! Modules run in a worker process, never in the server. Residency picks how long that process
//! lives. The worker hosts one shared library, so a library is treated as PERSISTENT if *any*
//! module it exports asks for it.
enum class ModuleResidency : std::uint8_t
{
	//! A fresh process per call, killed the moment the call returns. Nothing a module leaks or
	//! corrupts survives the call. The default: a module opts out of it, never into it.
	SINGLE_RUN,
	//! A long-lived worker reused across calls. For modules whose one-time initialisation is too
	//! expensive to pay per call (VIPS_INIT, codec registration). The host still retires the
	//! worker once it goes over its RSS ceiling or sits idle.
	PERSISTENT,
};

//! Semantic version of a module, reported by ModuleBase::version().
struct ModuleVersion
{
	std::size_t m_major { 1 };
	std::size_t m_minor { 0 };
	std::size_t m_patch { 0 };
};

//! Non-owning view over raw bytes. Still used by the mime scanner and by Blob, which genuinely do
//! hold whole buffers; it is no longer how a file reaches a module (see ModuleFile).
using data_view = std::basic_string_view< std::uint8_t >;

//! Input passed to a module for a single operation.
struct ModuleCallData
{
	//! The file being operated on. Valid only for the call's duration -- a module must not retain
	//! it, and reads through it after the call returns are undefined.
	const ModuleFile& file;
	std::string mime_name; //!< Canonical MIME type of the file, as resolved by the mime database.
	Json::Value extra; //!< Optional caller-supplied parameters; contents are operation-specific.
};

//! What the module system can do with a given blob, as answered by ModuleCallbacks::probe.
struct ModuleCapability
{
	std::string mime; //!< The MIME the host resolved the bytes to.
	bool has_metadata { false }; //!< Some module can parse metadata from these bytes.
	bool has_thumbnailer { false }; //!< Some module can thumbnail these bytes.
	bool has_generator { false }; //!< Some module can generate derived files from these bytes.
};

//! Host callbacks handed to every module at construction so it can re-dispatch work back through the
//! module system — e.g. an archive thumbnailer asking the host to thumbnail a contained file. The
//! host resolves the target module by MIME. These run synchronously and may re-enter the calling
//! module (see ModuleBase::threadSafe).
struct FGL_EXPORT ModuleCallbacks
{
	using ThumbnailFunc =
		std::function< std::expected< ThumbnailInfo, ModuleError >( const ModuleFile&, Json::Value, std::string ) >;
	//! Returns a handle rather than bytes, so a generated file the host produced in shared memory
	//! can be passed straight on to another callback without ever landing in this module's heap.
	using GenerateFunc = std::function< std::expected<
		std::unique_ptr< ModuleFile >,
		ModuleError >( const ModuleFile&, std::array< std::byte, 256 / 8 >, Json::Value, std::string ) >;
	using ProbeFunc = std::function< std::expected< ModuleCapability, ModuleError >( const ModuleFile&, std::string ) >;

	ThumbnailFunc thumbnail; //!< Ask the host to thumbnail the given bytes (data, extra, file_name).
	GenerateFunc generate; //!< Ask the host to generate a derived file matching the desired hash.
	//! Ask the host what, if anything, can handle these bytes (data, file_name) — one round trip,
	//! so a module can check before committing to a thumbnail or generate call it expects to fail.
	ProbeFunc probe;
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

	//! How the host should run this module (see ModuleResidency). Default SINGLE_RUN: a module has
	//! to justify keeping a process alive, and paying a fork per call is always correct if slow.
	[[nodiscard]] virtual ModuleResidency residency() { return ModuleResidency::SINGLE_RUN; }

	//! Called once after the library's init(), before the first call reaches this module.
	virtual void startup() {}

	//! Used for reclaiming resources. Called when the host is under memory pressure; drop caches
	//! but stay usable — this is not a teardown.
	virtual void restart() {}

	//! Called once before the worker process exits.
	virtual void shutdown() {}
};

using IDHANModule = ModuleBase;

} // namespace idhan
