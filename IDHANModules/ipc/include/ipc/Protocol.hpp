#pragma once

#include <json/value.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "MetadataInfo.hpp"
#include "ModuleBase.hpp"
#include "ThumbnailInfo.hpp"

// libvips defines IMAGE
#undef IMAGE

//! The wire protocol between the server and a module worker process.
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
inline constexpr auto MIME_ID { "mime_id" };
inline constexpr auto EXTRA { "extra" };
inline constexpr auto WIDTH { "width" };
inline constexpr auto HEIGHT { "height" };
inline constexpr auto HASH { "hash" };
inline constexpr auto DEPTH { "depth" };
inline constexpr auto FILE_NAME { "file_name" };
inline constexpr auto OK { "ok" };
inline constexpr auto ERROR { "error" };
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
inline constexpr auto RSS_CEILING_MB { "rss_ceiling_mb" };
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
inline constexpr auto FILE_SIZE { "file_size" };
inline constexpr auto INPUT_REF { "input_ref" };
inline constexpr auto EMBEDDING { "embedding" };
inline constexpr auto MODEL_NAME { "model_name" };
inline constexpr auto DIMENSIONS { "dimensions" };
inline constexpr auto PHRASE { "phrase" };
inline constexpr auto SUPPORTS_TEXT { "supports_text" };
inline constexpr auto FORMAT { "format" };
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
	HEARTBEAT, //!< Periodic proof of life, plus RSS for the recycling decision.
	RESULT, //!< A CALL finished. Carries the output blob, if any.
	CALLBACK, //!< A module is asking the host to re-dispatch work.
};

//! The module operations a CALL can request.
enum class CallOp : std::uint8_t
{
	METADATA, //!< MetadataModuleI::parseFile
	THUMB_RGBA, //!< ThumbnailerModuleI::createThumbnailRaw, raw interleaved RGB
	THUMB_FILE, //!< ThumbnailerModuleI::createThumbnailFile, encoded image
	GENERATE, //!< GeneratorModuleI::generate
	EMBED, //!< EmbeddingModuleI::embed
	EMBED_TEXT, //!< EmbeddingModuleI::embedText, the only op that carries no file
	MIME_PARSE, //!< MimeModuleI::parseMime
};

//! What a module is asking the host for when it sends a CALLBACK.
enum class CallbackKind : std::uint8_t
{
	PROBE, //!< ModuleCallbacks::probe
	THUMBNAIL, //!< ModuleCallbacks::thumbnail
	GENERATE, //!< ModuleCallbacks::generate
};

//! Wire tag for which alternative of MetadataInfo::m_metadata is populated.
enum class MetadataVariant : std::uint8_t
{
	NONE = 0,
	IMAGE = 1,
	VIDEO = 2,
	IMAGE_PROJECT = 3,
	ANIMATION = 4,
	ARCHIVE = 5,
	AUDIO = 6,
};

//! Whether \p value is one of the enumerators declared above.
//!@{
[[nodiscard]] constexpr bool isDeclared( const MessageType value ) noexcept
{
	switch ( value )
	{
		case MessageType::CALL:
		case MessageType::CALLBACK_RESULT:
		case MessageType::RECLAIM:
		case MessageType::SHUTDOWN:
		case MessageType::MANIFEST:
		case MessageType::HEARTBEAT:
		case MessageType::RESULT:
		case MessageType::CALLBACK:
			return true;
	}

	return false;
}

[[nodiscard]] constexpr bool isDeclared( const CallOp value ) noexcept
{
	switch ( value )
	{
		case CallOp::METADATA:
		case CallOp::THUMB_RGBA:
		case CallOp::THUMB_FILE:
		case CallOp::GENERATE:
		case CallOp::EMBED:
		case CallOp::EMBED_TEXT:
		case CallOp::MIME_PARSE:
			return true;
	}

	return false;
}

[[nodiscard]] constexpr bool isDeclared( const CallbackKind value ) noexcept
{
	switch ( value )
	{
		case CallbackKind::PROBE:
		case CallbackKind::THUMBNAIL:
		case CallbackKind::GENERATE:
			return true;
	}

	return false;
}

[[nodiscard]] constexpr bool isDeclared( const MetadataVariant value ) noexcept
{
	switch ( value )
	{
		case MetadataVariant::NONE:
		case MetadataVariant::IMAGE:
		case MetadataVariant::VIDEO:
		case MetadataVariant::IMAGE_PROJECT:
		case MetadataVariant::ANIMATION:
		case MetadataVariant::ARCHIVE:
		case MetadataVariant::AUDIO:
			return true;
	}

	return false;
}

[[nodiscard]] constexpr bool isDeclared( const ModuleResidency value ) noexcept
{
	switch ( value )
	{
		case ModuleResidency::SINGLE_RUN:
		case ModuleResidency::PERSISTENT:
			return true;
	}

	return false;
}

//!@}

//! Names for logs and error messages. Display only, never the wire encoding.
//!@{
[[nodiscard]] constexpr std::string_view toString( const MessageType value ) noexcept
{
	switch ( value )
	{
		case MessageType::CALL:
			return "call";
		case MessageType::CALLBACK_RESULT:
			return "callback_result";
		case MessageType::RECLAIM:
			return "reclaim";
		case MessageType::SHUTDOWN:
			return "shutdown";
		case MessageType::MANIFEST:
			return "manifest";
		case MessageType::HEARTBEAT:
			return "heartbeat";
		case MessageType::RESULT:
			return "result";
		case MessageType::CALLBACK:
			return "callback";
	}

	return "unknown";
}

[[nodiscard]] constexpr std::string_view toString( const CallOp value ) noexcept
{
	switch ( value )
	{
		case CallOp::METADATA:
			return "metadata";
		case CallOp::THUMB_RGBA:
			return "thumb_rgba";
		case CallOp::THUMB_FILE:
			return "thumb_file";
		case CallOp::GENERATE:
			return "generate";
		case CallOp::EMBED:
			return "embed";
		case CallOp::EMBED_TEXT:
			return "embed_text";
		case CallOp::MIME_PARSE:
			return "mime_parse";
	}

	return "unknown";
}

[[nodiscard]] constexpr std::string_view toString( const CallbackKind value ) noexcept
{
	switch ( value )
	{
		case CallbackKind::PROBE:
			return "probe";
		case CallbackKind::THUMBNAIL:
			return "thumbnail";
		case CallbackKind::GENERATE:
			return "generate";
	}

	return "unknown";
}

[[nodiscard]] constexpr std::string_view toString( const ModuleResidency value ) noexcept
{
	switch ( value )
	{
		case ModuleResidency::SINGLE_RUN:
			return "single_run";
		case ModuleResidency::PERSISTENT:
			return "persistent";
	}

	return "unknown";
}

//!@}

//! Renders a rejected enum field for a diagnostic without assuming it holds what it should.
[[nodiscard]] std::string describeWireValue( const Json::Value& json );

//! Encodes a protocol enum for the wire: its underlying integer, never its name.
template < typename EnumT >
	requires std::is_enum_v< EnumT >
[[nodiscard]] constexpr Json::UInt toWire( const EnumT value ) noexcept
{
	return static_cast< Json::UInt >( std::to_underlying( value ) );
}

//! Narrows a wire value back to \p EnumT, rejecting anything that is not a declared enumerator.
template < typename EnumT >
	requires std::is_enum_v< EnumT >
[[nodiscard]] std::optional< EnumT > fromWire( const Json::Value& json ) noexcept
{
	using Underlying = std::underlying_type_t< EnumT >;

	// isUInt64 rather than isIntegral: asUInt64 on a negative value trips a jsoncpp assert.
	if ( !json.isUInt64() ) return std::nullopt;

	const auto raw { json.asUInt64() };
	if ( raw > static_cast< Json::UInt64 >( std::numeric_limits< Underlying >::max() ) ) return std::nullopt;

	const auto value { static_cast< EnumT >( raw ) };
	if ( !isDeclared( value ) ) return std::nullopt;

	return value;
}

//! The interface flag a module must declare before \p op may be dispatched to it.
[[nodiscard]] constexpr ModuleType requiredFlag( const CallOp op ) noexcept
{
	switch ( op )
	{
		case CallOp::METADATA:
			return ModuleTypeFlags::METADATA;
		case CallOp::THUMB_RGBA:
			[[fallthrough]];
		case CallOp::THUMB_FILE:
			return ModuleTypeFlags::THUMBNAILER;
		case CallOp::GENERATE:
			return ModuleTypeFlags::GENERATOR;
		case CallOp::EMBED:
			[[fallthrough]];
		case CallOp::EMBED_TEXT:
			return ModuleTypeFlags::EMBEDDING;
		case CallOp::MIME_PARSE:
			return ModuleTypeFlags::MIME_PARSE;
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
	//! ModuleBase::rssCeilingMb(): the resident size this module needs to hold without being retired
	//! for it. Zero means it has no opinion and the configured ceiling stands. The host takes the
	//! largest value any module in the library declares, since they share one worker process.
	std::size_t rss_ceiling_mb { 0 };
	std::vector< MimeID > mimes {};
	//! EMBEDDING modules only; empty otherwise. The routing key for embed calls. Unlike `name`,
	//! this must be unique and stable, because it is also a database key.
	std::string model_name {};
	//! EMBEDDING modules only; zero otherwise. The width of the halfvec column built for this model.
	std::uint32_t dimensions { 0 };
	//! EMBEDDING modules only. Whether this model has a text tower, so the host can refuse text
	//! queries up front instead of discovering it from a failed call. False is a normal
	//! configuration: an image-only export, or a text tower that failed its parity check.
	bool supports_text { false };
};

[[nodiscard]] Json::Value toJson( const ManifestEntry& entry );
[[nodiscard]] std::expected< ManifestEntry, std::string > manifestEntryFromJson( const Json::Value& json );

//! An order-sensitive description of a whole manifest, used to detect a library changing on disk.
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
