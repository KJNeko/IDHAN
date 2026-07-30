//
// Created by kj16609 on 7/28/26.
//

#include "ipc/Protocol.hpp"

#include <format>
#include <type_traits>
#include <variant>

#include "crypto/simpleHasher.hpp"

namespace idhan::ipc
{

namespace
{

//! Wire tags for MetadataVariant's alternatives.
/** Tagged by name rather than by the variant's index on purpose: the index is a property of the
 *  declaration order in MetadataInfo.hpp, and inserting an alternative there would silently
 *  reinterpret every stored and in-flight message. */
constexpr auto VARIANT_NONE { "none" };
constexpr auto VARIANT_IMAGE { "image" };
constexpr auto VARIANT_VIDEO { "video" };
constexpr auto VARIANT_IMAGE_PROJECT { "image_project" };
constexpr auto VARIANT_ANIMATION { "animation" };
constexpr auto VARIANT_ARCHIVE { "archive" };

[[nodiscard]] Json::Value imageToJson( const MetadataInfoImage& image )
{
	Json::Value json {};
	json[ "width" ] = image.width;
	json[ "height" ] = image.height;
	json[ "channels" ] = static_cast< Json::UInt >( image.channels );
	return json;
}

[[nodiscard]] MetadataInfoImage imageFromJson( const Json::Value& json )
{
	return MetadataInfoImage {
		.width = json[ "width" ].asInt(),
		.height = json[ "height" ].asInt(),
		.channels = static_cast< std::uint8_t >( json[ "channels" ].asUInt() )
	};
}

} // namespace

std::string_view toString( const MessageType value ) noexcept
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
		case MessageType::ACK:
			return "ack";
		case MessageType::HEARTBEAT:
			return "heartbeat";
		case MessageType::RESULT:
			return "result";
		case MessageType::CALLBACK:
			return "callback";
	}

	return "unknown";
}

std::string_view toString( const CallOp value ) noexcept
{
	switch ( value )
	{
		case CallOp::METADATA:
			return "metadata";
		case CallOp::THUMB_RAW:
			return "thumb_raw";
		case CallOp::THUMB_FILE:
			return "thumb_file";
		case CallOp::GENERATE:
			return "generate";
	}

	return "unknown";
}

std::string_view toString( const CallbackKind value ) noexcept
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

std::string_view toString( const ModuleResidency value ) noexcept
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

std::optional< MessageType > messageTypeFromString( const std::string_view value ) noexcept
{
	if ( value == "call" ) return MessageType::CALL;
	if ( value == "callback_result" ) return MessageType::CALLBACK_RESULT;
	if ( value == "reclaim" ) return MessageType::RECLAIM;
	if ( value == "shutdown" ) return MessageType::SHUTDOWN;
	if ( value == "manifest" ) return MessageType::MANIFEST;
	if ( value == "ack" ) return MessageType::ACK;
	if ( value == "heartbeat" ) return MessageType::HEARTBEAT;
	if ( value == "result" ) return MessageType::RESULT;
	if ( value == "callback" ) return MessageType::CALLBACK;

	return std::nullopt;
}

std::optional< CallOp > callOpFromString( const std::string_view value ) noexcept
{
	if ( value == "metadata" ) return CallOp::METADATA;
	if ( value == "thumb_raw" ) return CallOp::THUMB_RAW;
	if ( value == "thumb_file" ) return CallOp::THUMB_FILE;
	if ( value == "generate" ) return CallOp::GENERATE;

	return std::nullopt;
}

std::optional< CallbackKind > callbackKindFromString( const std::string_view value ) noexcept
{
	if ( value == "probe" ) return CallbackKind::PROBE;
	if ( value == "thumbnail" ) return CallbackKind::THUMBNAIL;
	if ( value == "generate" ) return CallbackKind::GENERATE;

	return std::nullopt;
}

std::optional< ModuleResidency > residencyFromString( const std::string_view value ) noexcept
{
	if ( value == "single_run" ) return ModuleResidency::SINGLE_RUN;
	if ( value == "persistent" ) return ModuleResidency::PERSISTENT;

	return std::nullopt;
}

Json::Value toJson( const ModuleVersion& version )
{
	Json::Value json {};
	json[ field::MAJOR ] = static_cast< Json::UInt64 >( version.m_major );
	json[ field::MINOR ] = static_cast< Json::UInt64 >( version.m_minor );
	json[ field::PATCH ] = static_cast< Json::UInt64 >( version.m_patch );
	return json;
}

ModuleVersion moduleVersionFromJson( const Json::Value& json )
{
	return ModuleVersion {
		.m_major = static_cast< std::size_t >( json[ field::MAJOR ].asUInt64() ),
		.m_minor = static_cast< std::size_t >( json[ field::MINOR ].asUInt64() ),
		.m_patch = static_cast< std::size_t >( json[ field::PATCH ].asUInt64() )
	};
}

Json::Value toJson( const ManifestEntry& entry )
{
	Json::Value json {};
	json[ field::INDEX ] = static_cast< Json::UInt64 >( entry.index );
	json[ field::NAME ] = entry.name;
	json[ field::TYPE ] = static_cast< Json::UInt >( entry.type );
	json[ field::VERSION ] = toJson( entry.version );
	json[ field::THREAD_SAFE ] = entry.thread_safe;
	json[ field::RESIDENCY ] = std::string { toString( entry.residency ) };

	// arrayValue explicitly: a default-constructed Json::Value stays null until something is
	// appended, so a module that handles no MIME types would serialise as null rather than [].
	Json::Value mimes { Json::arrayValue };
	for ( const auto& mime : entry.mimes ) mimes.append( mime );
	json[ field::MIMES ] = mimes;

	return json;
}

std::expected< ManifestEntry, std::string > manifestEntryFromJson( const Json::Value& json )
{
	if ( !json.isObject() ) return std::unexpected( std::string { "manifest entry was not an object" } );

	if ( !json[ field::INDEX ].isIntegral() )
		return std::unexpected( std::string { "manifest entry has no integral index" } );

	if ( !json[ field::NAME ].isString() ) return std::unexpected( std::string { "manifest entry has no name" } );

	if ( !json[ field::TYPE ].isIntegral() )
		return std::unexpected( std::string { "manifest entry has no integral type" } );

	const auto residency { residencyFromString( json[ field::RESIDENCY ].asString() ) };
	if ( !residency )
		return std::unexpected(
			std::format( "manifest entry has unknown residency '{}'", json[ field::RESIDENCY ].asString() ) );

	ManifestEntry entry {};
	entry.index = static_cast< std::size_t >( json[ field::INDEX ].asUInt64() );
	entry.name = json[ field::NAME ].asString();
	entry.type = static_cast< ModuleType >( json[ field::TYPE ].asUInt() );
	entry.version = moduleVersionFromJson( json[ field::VERSION ] );
	entry.thread_safe = json[ field::THREAD_SAFE ].asBool();
	entry.residency = *residency;

	const auto& mimes { json[ field::MIMES ] };
	if ( !mimes.isArray() ) return std::unexpected( std::string { "manifest entry has no mime array" } );

	for ( const auto& mime : mimes )
	{
		if ( !mime.isString() ) return std::unexpected( std::string { "manifest entry has a non-string mime" } );
		entry.mimes.emplace_back( mime.asString() );
	}

	return entry;
}

std::string manifestSignature( const std::vector< ManifestEntry >& entries )
{
	std::string signature {};

	for ( const auto& entry : entries )
	{
		signature += std::format(
			"{}|{}|{}|{}.{}.{}|",
			entry.index,
			entry.name,
			entry.type,
			entry.version.m_major,
			entry.version.m_minor,
			entry.version.m_patch );

		for ( const auto& mime : entry.mimes )
		{
			signature += mime;
			signature += ',';
		}

		signature += '\n';
	}

	return signature;
}

Json::Value toJson( const ModuleCapability& capability )
{
	Json::Value json {};
	json[ field::MIME ] = capability.mime;
	json[ field::HAS_METADATA ] = capability.has_metadata;
	json[ field::HAS_THUMBNAILER ] = capability.has_thumbnailer;
	json[ field::HAS_GENERATOR ] = capability.has_generator;
	return json;
}

std::expected< ModuleCapability, std::string > capabilityFromJson( const Json::Value& json )
{
	if ( !json.isObject() ) return std::unexpected( std::string { "capability was not an object" } );
	if ( !json[ field::MIME ].isString() ) return std::unexpected( std::string { "capability has no mime" } );

	return ModuleCapability {
		.mime = json[ field::MIME ].asString(),
		.has_metadata = json[ field::HAS_METADATA ].asBool(),
		.has_thumbnailer = json[ field::HAS_THUMBNAILER ].asBool(),
		.has_generator = json[ field::HAS_GENERATOR ].asBool()
	};
}

Json::Value toJson( const MetadataInfo& info )
{
	Json::Value json {};
	json[ field::SIMPLE_TYPE ] = static_cast< Json::UInt >( info.m_simple_type );
	json[ field::EXTRA ] = info.m_extra;

	Json::Value value {};
	std::string tag { VARIANT_NONE };

	std::visit(
		[ &tag, &value ]( const auto& metadata )
		{
			using T = std::decay_t< decltype( metadata ) >;

			if constexpr ( std::is_same_v< T, MetadataInfoImage > )
			{
				tag = VARIANT_IMAGE;
				value = imageToJson( metadata );
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoVideo > )
			{
				tag = VARIANT_VIDEO;
				value[ "has_audio" ] = metadata.m_has_audio;
				value[ "width" ] = metadata.m_width;
				value[ "height" ] = metadata.m_height;
				value[ "bitrate" ] = metadata.m_bitrate;
				value[ "duration" ] = metadata.m_duration;
				value[ "fps" ] = metadata.m_fps;
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoImageProject > )
			{
				tag = VARIANT_IMAGE_PROJECT;
				value[ "image" ] = imageToJson( metadata.image_info );
				value[ "layers" ] = static_cast< Json::UInt >( metadata.layers );
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoAnimation > )
			{
				tag = VARIANT_ANIMATION;
				value = Json::Value { Json::objectValue };
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoArchive > )
			{
				tag = VARIANT_ARCHIVE;

				Json::Value hashes { Json::arrayValue };
				for ( const auto& hash : metadata.contained_hashes ) hashes.append( crypto::toHex( hash ) );

				value[ "contained_hashes" ] = hashes;
				value[ "size" ] = static_cast< Json::UInt64 >( metadata.m_size );
				value[ "encrypted" ] = metadata.encrypted;
			}
			else
			{
				tag = VARIANT_NONE;
				value = Json::Value { Json::objectValue };
			}
		},
		info.m_metadata );

	json[ field::VARIANT ] = tag;
	json[ field::VALUE ] = value;

	return json;
}

std::expected< MetadataInfo, std::string > metadataInfoFromJson( const Json::Value& json )
{
	if ( !json.isObject() ) return std::unexpected( std::string { "metadata was not an object" } );

	MetadataInfo info {};
	info.m_simple_type = static_cast< SimpleMimeType >( json[ field::SIMPLE_TYPE ].asUInt() );
	info.m_extra = json[ field::EXTRA ];

	if ( !json[ field::VARIANT ].isString() ) return std::unexpected( std::string { "metadata has no variant tag" } );

	const auto tag { json[ field::VARIANT ].asString() };
	const auto& value { json[ field::VALUE ] };

	if ( tag == VARIANT_NONE )
	{
		info.m_metadata = std::monostate {};
	}
	else if ( tag == VARIANT_IMAGE )
	{
		info.m_metadata = imageFromJson( value );
	}
	else if ( tag == VARIANT_VIDEO )
	{
		info.m_metadata = MetadataInfoVideo {
			.m_has_audio = value[ "has_audio" ].asBool(),
			.m_width = value[ "width" ].asInt(),
			.m_height = value[ "height" ].asInt(),
			.m_bitrate = value[ "bitrate" ].asInt(),
			.m_duration = value[ "duration" ].asDouble(),
			.m_fps = value[ "fps" ].asDouble()
		};
	}
	else if ( tag == VARIANT_IMAGE_PROJECT )
	{
		info.m_metadata = MetadataInfoImageProject {
			.image_info = imageFromJson( value[ "image" ] ),
			.layers = static_cast< std::uint8_t >( value[ "layers" ].asUInt() )
		};
	}
	else if ( tag == VARIANT_ANIMATION )
	{
		info.m_metadata = MetadataInfoAnimation {};
	}
	else if ( tag == VARIANT_ARCHIVE )
	{
		MetadataInfoArchive archive {};

		const auto& hashes { value[ "contained_hashes" ] };
		if ( !hashes.isArray() ) return std::unexpected( std::string { "archive metadata has no hash array" } );

		for ( const auto& hash : hashes )
		{
			if ( !hash.isString() ) return std::unexpected( std::string { "archive metadata has a non-string hash" } );

			const auto hex { hash.asString() };
			if ( hex.size() != ( 256 / 8 ) * 2 )
				return std::unexpected( std::format( "archive metadata hash '{}' is not a sha256 hex string", hex ) );

			archive.contained_hashes.emplace_back( crypto::fromHex( hex ) );
		}

		archive.m_size = static_cast< std::size_t >( value[ "size" ].asUInt64() );
		archive.encrypted = value[ "encrypted" ].asBool();

		info.m_metadata = std::move( archive );
	}
	else
	{
		return std::unexpected( std::format( "metadata has unknown variant tag '{}'", tag ) );
	}

	return info;
}

Json::Value thumbnailHeaderToJson( const ThumbnailInfo& info )
{
	Json::Value json {};
	json[ field::WIDTH ] = static_cast< Json::UInt64 >( info.width );
	json[ field::HEIGHT ] = static_cast< Json::UInt64 >( info.height );
	json[ field::CACHE_THUMBNAIL ] = info.cache_thumbnail;
	return json;
}

std::expected< ThumbnailInfo, std::string > thumbnailFromJson(
	const Json::Value& json,
	std::vector< std::byte > pixels )
{
	if ( !json.isObject() ) return std::unexpected( std::string { "thumbnail header was not an object" } );

	ThumbnailInfo info {};
	info.width = static_cast< std::size_t >( json[ field::WIDTH ].asUInt64() );
	info.height = static_cast< std::size_t >( json[ field::HEIGHT ].asUInt64() );
	info.cache_thumbnail = json[ field::CACHE_THUMBNAIL ].asBool();
	info.m_pixel_data = std::move( pixels );

	return info;
}

} // namespace idhan::ipc
