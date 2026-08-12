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

using data_view = std::basic_string_view< std::uint8_t >;

struct ModuleCallData
{
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

using ThumbnailFunc =
	std::function< std::expected< ThumbnailInfo, ModuleError >( const ModuleFile&, Json::Value, std::string ) >;

namespace module
{
using SHA256 = std::array< std::byte, ( 256 / 8 ) >;
}

using GenerateFunc = std::function< std::expected<
	std::unique_ptr< ModuleFile >,
	ModuleError >( const ModuleFile&, module::SHA256, Json::Value, std::string ) >;

using ProbeFunc = std::function< std::expected< ModuleCapability, ModuleError >( const ModuleFile&, std::string ) >;

struct FGL_EXPORT ModuleCallbacks
{
	ThumbnailFunc thumbnail;
	GenerateFunc generate;
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

	//! Resident size, in MiB, this module needs to hold steadily without being retired for it.
	/** The host retires a persistent worker that goes over an RSS ceiling, which is what bounds a
	 *  leak rather than merely isolating it. That ceiling is one global number sized for the media
	 *  modules, where hundreds of megabytes already means something has gone wrong. A module whose
	 *  normal working set is larger -- an inference module holding model weights resident -- would be
	 *  reaped at its first quiescent moment and reload them on the next call, which is precisely the
	 *  cost PERSISTENT exists to avoid.
	 *
	 *  Returning non-zero raises the ceiling for this module's worker to at least this much; the
	 *  configured limit still applies where it is higher. This is a floor on the module's normal
	 *  footprint, NOT a budget: report what the module holds when it is behaving, so that anything
	 *  above it is still recognisable as a leak and still bounded.
	 *
	 *  Answered before startup(), like the rest of the manifest, so it must come from configuration
	 *  or from what is on disk rather than from anything learned by loading. */
	[[nodiscard]] virtual std::size_t rssCeilingMb() { return 0; }

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
