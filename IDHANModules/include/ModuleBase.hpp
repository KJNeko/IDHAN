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

#include "IDHANTypes.hpp"
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
	MIME_PARSE = 1 << 4, //! Implements MimeModuleI::parseMime
};

//! Bitwise-OR of ModuleTypeFlags values; the concrete return type of ModuleBase::type().
using ModuleType = std::uint16_t;

//! How the host runs a module, as reported by ModuleBase::residency().
enum class ModuleResidency : std::uint8_t
{
	//! A fresh process per call, killed the moment the call returns. The default: a module
	//! opts out of it, never into it.
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
	//! The file's type, one of the constants in MimeIDs.hpp. Mime strings never reach a module:
	//! the pre-scan resolves one to an id before anything is dispatched.
	MimeID mime_id { 0 };
	Json::Value extra; //!< Optional caller-supplied parameters; contents are operation-specific.
};

//! What the module system can do with a given blob, as answered by ModuleCallbacks::probe.
struct ModuleCapability
{
	MimeID mime_id { 0 }; //!< The mime id the host resolved the bytes to.
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

	[[nodiscard]] virtual bool threadSafe() { return false; }

	//! The interfaces this module implements, as an OR of ModuleTypeFlags.
	[[nodiscard]] virtual ModuleType type() = 0;

	//! The module's semantic version.
	[[nodiscard]] virtual ModuleVersion version() = 0;

	//! How the host should run this module (see ModuleResidency).
	[[nodiscard]] virtual ModuleResidency residency() { return ModuleResidency::SINGLE_RUN; }

	//! Resident size, in MiB, this module needs to hold steadily without being retired for it.
	[[nodiscard]] virtual std::size_t rssCeilingMb() { return 0; }

	//! Called once after the library's init(), before the first call reaches this module.
	virtual void startup() {}

	//! Used for reclaiming resources. Called when the host is under memory pressure; drop caches
	//! but stay usable. This is not a teardown.
	virtual void restart() {}

	//! Called once before the worker process exits.
	virtual void shutdown() {}
};

using IDHANModule = ModuleBase;

} // namespace idhan
