//
// Created by kj16609 on 7/28/26.
//
#pragma once

#include <json/value.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "MetadataInfo.hpp"
#include "ModuleBase.hpp"
#include "ThumbnailInfo.hpp"

//! The wire protocol between the server and a module worker process.
/** Bodies are JSON and bulk data is a blob descriptor, never inline. JSON costs more than a packed
 *  binary encoding would, but every message here is a control message -- an operation name, a MIME,
 *  a few integers -- so the cost is noise next to the work it describes, and a protocol you can
 *  read in a log is worth a great deal when the thing you are debugging lives in another process. */
namespace idhan::ipc
{

//! Wire field names, in one place so both ends cannot drift.
namespace field
{
inline constexpr auto TYPE { "type" };
inline constexpr auto CALL_ID { "call_id" };
inline constexpr auto CALLBACK_ID { "cb_id" };
inline constexpr auto MODULE_INDEX { "module_index" };
inline constexpr auto OP { "op" };
inline constexpr auto KIND { "kind" };
inline constexpr auto MIME { "mime" };
inline constexpr auto EXTRA { "extra" };
inline constexpr auto WIDTH { "width" };
inline constexpr auto HEIGHT { "height" };
inline constexpr auto HASH { "hash" };
inline constexpr auto DEPTH { "depth" };
inline constexpr auto FILE_NAME { "file_name" };
inline constexpr auto OK { "ok" };
inline constexpr auto ERROR { "error" };
inline constexpr auto ESTIMATE_MS { "estimate_ms" };
inline constexpr auto RSS_KB { "rss_kb" };
inline constexpr auto ACTIVE_CALLS { "active_calls" };
inline constexpr auto MODULES { "modules" };
inline constexpr auto INDEX { "index" };
inline constexpr auto NAME { "name" };
inline constexpr auto VERSION { "version" };
inline constexpr auto MAJOR { "major" };
inline constexpr auto MINOR { "minor" };
inline constexpr auto PATCH { "patch" };
inline constexpr auto THREAD_SAFE { "thread_safe" };
inline constexpr auto RESIDENCY { "residency" };
inline constexpr auto MIMES { "mimes" };
inline constexpr auto METADATA { "metadata" };
inline constexpr auto THUMBNAIL { "thumbnail" };
inline constexpr auto CAPABILITY { "capability" };
inline constexpr auto HAS_METADATA { "has_metadata" };
inline constexpr auto HAS_THUMBNAILER { "has_thumbnailer" };
inline constexpr auto HAS_GENERATOR { "has_generator" };
inline constexpr auto CACHE_THUMBNAIL { "cache_thumbnail" };
inline constexpr auto SIMPLE_TYPE { "simple_type" };
inline constexpr auto VARIANT { "variant" };
inline constexpr auto VALUE { "value" };
} // namespace field

//! Every message the protocol carries.
enum class MessageType : std::uint8_t
{
	// host -> worker
	CALL, //!< Run one module operation. Carries the input blob.
	CALLBACK_RESULT, //!< The answer to a CALLBACK the worker sent.
	RECLAIM, //!< Ask every module to drop caches (ModuleBase::restart).
	SHUTDOWN, //!< Finish outstanding work and exit.

	// worker -> host
	MANIFEST, //!< Sent unsolicited at startup: what this library exports.
	ACK, //!< A CALL was picked up, with the module's own duration estimate.
	HEARTBEAT, //!< Periodic proof of life, plus RSS for the recycling decision.
	RESULT, //!< A CALL finished. Carries the output blob, if any.
	CALLBACK, //!< A module is asking the host to re-dispatch work.
};

//! The module operations a CALL can request.
enum class CallOp : std::uint8_t
{
	METADATA, //!< MetadataModuleI::parseFile
	THUMB_RAW, //!< ThumbnailerModuleI::createThumbnailRaw -- raw interleaved RGB
	THUMB_FILE, //!< ThumbnailerModuleI::createThumbnailFile -- encoded image
	GENERATE, //!< GeneratorModuleI::generate
};

//! What a module is asking the host for when it sends a CALLBACK.
enum class CallbackKind : std::uint8_t
{
	PROBE, //!< ModuleCallbacks::probe
	THUMBNAIL, //!< ModuleCallbacks::thumbnail
	GENERATE, //!< ModuleCallbacks::generate
};

[[nodiscard]] std::string_view toString( MessageType value ) noexcept;
[[nodiscard]] std::string_view toString( CallOp value ) noexcept;
[[nodiscard]] std::string_view toString( CallbackKind value ) noexcept;
[[nodiscard]] std::string_view toString( ModuleResidency value ) noexcept;

[[nodiscard]] std::optional< MessageType > messageTypeFromString( std::string_view value ) noexcept;
[[nodiscard]] std::optional< CallOp > callOpFromString( std::string_view value ) noexcept;
[[nodiscard]] std::optional< CallbackKind > callbackKindFromString( std::string_view value ) noexcept;
[[nodiscard]] std::optional< ModuleResidency > residencyFromString( std::string_view value ) noexcept;

//! The interface flag a module must declare before \p op may be dispatched to it.
/** The worker checks this before downcasting. A module_index that does not match the op is a host
 *  bug, but it arrives over a socket, and turning it into an error response rather than a bad
 *  static_pointer_cast is the difference between a failed request and a corrupted worker. */
[[nodiscard]] constexpr ModuleType requiredFlag( const CallOp op ) noexcept
{
	switch ( op )
	{
		case CallOp::METADATA:
			return ModuleTypeFlags::METADATA;
		case CallOp::THUMB_RAW:
			[[fallthrough]];
		case CallOp::THUMB_FILE:
			return ModuleTypeFlags::THUMBNAILER;
		case CallOp::GENERATE:
			return ModuleTypeFlags::GENERATOR;
	}

	return 0;
}

//! One module, as a worker describes it in its MANIFEST.
struct ManifestEntry
{
	//! Position in the vector the library's factory returns. This is the routing key: a CALL names
	//! a module by this index and nothing else.
	std::size_t index { 0 };
	std::string name {}; //!< For logs. Not unique, not stable, never used to route.
	ModuleType type { 0 };
	ModuleVersion version {};
	bool thread_safe { false };
	ModuleResidency residency { ModuleResidency::SINGLE_RUN };
	std::vector< std::string > mimes {};
};

[[nodiscard]] Json::Value toJson( const ManifestEntry& entry );
[[nodiscard]] std::expected< ManifestEntry, std::string > manifestEntryFromJson( const Json::Value& json );

//! An order-sensitive description of a whole manifest, used to detect a library changing on disk.
/** Module indexes only mean anything relative to the factory that produced them. If a library is
 *  rebuilt while the server is running -- routine during development -- an index registered against
 *  the old build can silently address a different module in the new one. Every worker resends its
 *  manifest at startup and the host compares this string; a mismatch fails the call loudly instead
 *  of quietly thumbnailing with the wrong module. */
[[nodiscard]] std::string manifestSignature( const std::vector< ManifestEntry >& entries );

[[nodiscard]] Json::Value toJson( const ModuleVersion& version );
[[nodiscard]] ModuleVersion moduleVersionFromJson( const Json::Value& json );

[[nodiscard]] Json::Value toJson( const ModuleCapability& capability );
[[nodiscard]] std::expected< ModuleCapability, std::string > capabilityFromJson( const Json::Value& json );

[[nodiscard]] Json::Value toJson( const MetadataInfo& info );
[[nodiscard]] std::expected< MetadataInfo, std::string > metadataInfoFromJson( const Json::Value& json );

//! Serialises everything about a thumbnail except its pixels, which travel as a blob.
[[nodiscard]] Json::Value thumbnailHeaderToJson( const ThumbnailInfo& info );

//! Rebuilds a ThumbnailInfo from its header and the pixels that arrived alongside it.
[[nodiscard]] std::expected< ThumbnailInfo, std::string > thumbnailFromJson(
	const Json::Value& json,
	std::vector< std::byte > pixels );

} // namespace idhan::ipc
