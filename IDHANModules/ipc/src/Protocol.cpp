#include "ipc/Protocol.hpp"

#include <format>
#include <type_traits>
#include <variant>

#include "crypto/simpleHasher.hpp"

namespace idhan::ipc
{

std::string describeWireValue( const Json::Value& json )
{
	if ( json.isNull() ) return "absent";
	if ( json.isUInt64() ) return std::to_string( json.asUInt64() );
	if ( json.isInt64() ) return std::to_string( json.asInt64() );

	return "a non-integral value";
}

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
	json[ field::RESIDENCY ] = toWire( entry.residency );
	json[ field::RSS_CEILING_MB ] = static_cast< Json::UInt64 >( entry.rss_ceiling_mb );

	Json::Value mimes { Json::arrayValue };
	for ( const auto mime_id : entry.mimes ) mimes.append( static_cast< Json::Int >( mime_id ) );
	json[ field::MIMES ] = mimes;

	json[ field::MODEL_NAME ] = entry.model_name;
	json[ field::DIMENSIONS ] = static_cast< Json::UInt >( entry.dimensions );
	json[ field::SUPPORTS_TEXT ] = entry.supports_text;

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

	const auto residency { fromWire< ModuleResidency >( json[ field::RESIDENCY ] ) };
	if ( !residency )
		return std::unexpected(
			std::format(
				"manifest entry has an unknown residency: {}", describeWireValue( json[ field::RESIDENCY ] ) ) );

	ManifestEntry entry {};
	entry.index = static_cast< std::size_t >( json[ field::INDEX ].asUInt64() );
	entry.name = json[ field::NAME ].asString();
	entry.type = static_cast< ModuleType >( json[ field::TYPE ].asUInt() );
	entry.version = moduleVersionFromJson( json[ field::VERSION ] );
	entry.thread_safe = json[ field::THREAD_SAFE ].asBool();
	entry.residency = *residency;

	if ( json.isMember( field::RSS_CEILING_MB ) )
	{
		if ( !json[ field::RSS_CEILING_MB ].isIntegral() )
			return std::unexpected( std::string { "manifest entry has a non-integral rss_ceiling_mb" } );

		entry.rss_ceiling_mb = static_cast< std::size_t >( json[ field::RSS_CEILING_MB ].asUInt64() );
	}

	if ( json.isMember( field::MODEL_NAME ) )
	{
		if ( !json[ field::MODEL_NAME ].isString() )
			return std::unexpected( std::string { "manifest entry has a non-string model_name" } );

		entry.model_name = json[ field::MODEL_NAME ].asString();
	}

	if ( json.isMember( field::DIMENSIONS ) )
	{
		if ( !json[ field::DIMENSIONS ].isIntegral() )
			return std::unexpected( std::string { "manifest entry has a non-integral dimensions" } );

		entry.dimensions = static_cast< std::uint32_t >( json[ field::DIMENSIONS ].asUInt() );
	}

	if ( json.isMember( field::SUPPORTS_TEXT ) ) entry.supports_text = json[ field::SUPPORTS_TEXT ].asBool();

	if ( ( entry.type & ModuleTypeFlags::EMBEDDING ) != 0 )
	{
		if ( entry.model_name.empty() )
			return std::unexpected( std::string { "embedding module declared no model_name" } );

		if ( entry.dimensions == 0 )
			return std::unexpected( std::format( "embedding module '{}' declared zero dimensions", entry.model_name ) );
	}

	const auto& mimes { json[ field::MIMES ] };
	if ( !mimes.isArray() ) return std::unexpected( std::string { "manifest entry has no mime array" } );

	for ( const auto& mime : mimes )
	{
		if ( !mime.isIntegral() )
			return std::unexpected(
				std::format( "manifest entry has a non-numeric mime id: {}", describeWireValue( mime ) ) );
		entry.mimes.emplace_back( static_cast< MimeID >( mime.asInt() ) );
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

		for ( const auto mime_id : entry.mimes )
		{
			signature += std::to_string( mime_id );
			signature += ',';
		}

		signature += std::format( "|{}|{}|{}", entry.model_name, entry.dimensions, entry.supports_text );

		signature += '\n';
	}

	return signature;
}

Json::Value toJson( const ModuleCapability& capability )
{
	Json::Value json {};
	json[ field::MIME_ID ] = static_cast< Json::Int >( capability.mime_id );
	json[ field::HAS_METADATA ] = capability.has_metadata;
	json[ field::HAS_THUMBNAILER ] = capability.has_thumbnailer;
	json[ field::HAS_GENERATOR ] = capability.has_generator;
	return json;
}

std::expected< ModuleCapability, std::string > capabilityFromJson( const Json::Value& json )
{
	if ( !json.isObject() ) return std::unexpected( std::string { "capability was not an object" } );
	if ( !json[ field::MIME_ID ].isIntegral() ) return std::unexpected( std::string { "capability has no mime id" } );

	return ModuleCapability {
		.mime_id = static_cast< MimeID >( json[ field::MIME_ID ].asInt() ),
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
	MetadataVariant tag { MetadataVariant::NONE };

	std::visit(
		[ &tag, &value ]( const auto& metadata )
		{
			using T = std::decay_t< decltype( metadata ) >;

			if constexpr ( std::is_same_v< T, MetadataInfoImage > )
			{
				tag = MetadataVariant::IMAGE;
				value = imageToJson( metadata );
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoVideo > )
			{
				tag = MetadataVariant::VIDEO;
				value[ "has_audio" ] = metadata.m_has_audio;
				value[ "width" ] = metadata.m_width;
				value[ "height" ] = metadata.m_height;
				value[ "bitrate" ] = metadata.m_bitrate;
				value[ "duration" ] = metadata.m_duration;
				value[ "fps" ] = metadata.m_fps;
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoImageProject > )
			{
				tag = MetadataVariant::IMAGE_PROJECT;
				value[ "image" ] = imageToJson( metadata.image_info );
				value[ "layers" ] = static_cast< Json::UInt >( metadata.layers );
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoAnimation > )
			{
				tag = MetadataVariant::ANIMATION;
				value[ "width" ] = metadata.width;
				value[ "height" ] = metadata.height;
				value[ "frame_count" ] = metadata.frame_count;
				value[ "duration" ] = metadata.duration;
				value[ "loops" ] = metadata.loops;
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoArchive > )
			{
				tag = MetadataVariant::ARCHIVE;

				Json::Value hashes { Json::arrayValue };
				for ( const auto& hash : metadata.contained_hashes ) hashes.append( crypto::toHex( hash ) );

				value[ "contained_hashes" ] = hashes;
				value[ "size" ] = static_cast< Json::UInt64 >( metadata.m_size );
				value[ "encrypted" ] = metadata.encrypted;
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoAudio > )
			{
				tag = MetadataVariant::AUDIO;
				value[ "duration" ] = metadata.m_duration;
				value[ "bitrate" ] = metadata.m_bitrate;
				value[ "channels" ] = static_cast< Json::UInt >( metadata.m_channels );
				value[ "sample_rate" ] = metadata.m_sample_rate;
			}
			else
			{
				tag = MetadataVariant::NONE;
				value = Json::Value { Json::objectValue };
			}
		},
		info.m_metadata );

	json[ field::VARIANT ] = toWire( tag );
	json[ field::VALUE ] = value;

	return json;
}

std::expected< MetadataInfo, std::string > metadataInfoFromJson( const Json::Value& json )
{
	if ( !json.isObject() ) return std::unexpected( std::string { "metadata was not an object" } );

	MetadataInfo info {};
	info.m_simple_type = static_cast< SimpleMimeType >( json[ field::SIMPLE_TYPE ].asUInt() );
	info.m_extra = json[ field::EXTRA ];

	const auto decoded { fromWire< MetadataVariant >( json[ field::VARIANT ] ) };
	if ( !decoded )
		return std::unexpected(
			std::format( "metadata has an unknown variant tag: {}", describeWireValue( json[ field::VARIANT ] ) ) );

	const auto& value { json[ field::VALUE ] };

	switch ( *decoded )
	{
		case MetadataVariant::NONE:
			info.m_metadata = std::monostate {};
			break;
		case MetadataVariant::IMAGE:
			info.m_metadata = imageFromJson( value );
			break;
		case MetadataVariant::VIDEO:
			info.m_metadata = MetadataInfoVideo {
				.m_has_audio = value[ "has_audio" ].asBool(),
				.m_width = value[ "width" ].asInt(),
				.m_height = value[ "height" ].asInt(),
				.m_bitrate = value[ "bitrate" ].asInt(),
				.m_duration = value[ "duration" ].asDouble(),
				.m_fps = value[ "fps" ].asDouble()
			};
			break;
		case MetadataVariant::IMAGE_PROJECT:
			info.m_metadata = MetadataInfoImageProject {
				.image_info = imageFromJson( value[ "image" ] ),
				.layers = static_cast< std::uint8_t >( value[ "layers" ].asUInt() )
			};
			break;
		case MetadataVariant::ANIMATION:
			info.m_metadata = MetadataInfoAnimation {
				.width = value[ "width" ].asInt(),
				.height = value[ "height" ].asInt(),
				.frame_count = value[ "frame_count" ].asInt(),
				.duration = value[ "duration" ].asDouble(),
				.loops = value[ "loops" ].asBool()
			};
			break;
		case MetadataVariant::AUDIO:
			info.m_metadata = MetadataInfoAudio {
				.m_duration = value[ "duration" ].asDouble(),
				.m_bitrate = value[ "bitrate" ].asInt(),
				.m_channels = static_cast< std::uint8_t >( value[ "channels" ].asUInt() ),
				.m_sample_rate = value[ "sample_rate" ].asInt()
			};
			break;
		case MetadataVariant::ARCHIVE:
			{
				MetadataInfoArchive archive {};

				const auto& hashes { value[ "contained_hashes" ] };
				if ( !hashes.isArray() ) return std::unexpected( std::string { "archive metadata has no hash array" } );

				for ( const auto& hash : hashes )
				{
					if ( !hash.isString() )
						return std::unexpected( std::string { "archive metadata has a non-string hash" } );

					const auto hex { hash.asString() };
					if ( hex.size() != ( 256 / 8 ) * 2 )
						return std::unexpected(
							std::format( "archive metadata hash '{}' is not a sha256 hex string", hex ) );

					archive.contained_hashes.emplace_back( crypto::fromHex( hex ) );
				}

				archive.m_size = static_cast< std::size_t >( value[ "size" ].asUInt64() );
				archive.encrypted = value[ "encrypted" ].asBool();

				info.m_metadata = std::move( archive );
				break;
			}
	}

	return info;
}

Json::Value thumbnailHeaderToJson( const ThumbnailInfo& info )
{
	Json::Value json {};
	json[ field::WIDTH ] = static_cast< Json::UInt64 >( info.width );
	json[ field::HEIGHT ] = static_cast< Json::UInt64 >( info.height );
	json[ field::CACHE_THUMBNAIL ] = info.cache_thumbnail;
	json[ field::FORMAT ] = static_cast< Json::UInt >( static_cast< std::uint8_t >( info.m_format ) );
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

	// An older worker sends no format field; RGB is what every module produced before ANIMATED existed.
	if ( const auto& format { json[ field::FORMAT ] }; format.isIntegral() )
	{
		const auto raw { format.asUInt() };
		if ( raw > static_cast< std::uint8_t >( ThumbnailFormat::ANIMATED ) )
			return std::unexpected( std::format( "thumbnail header carried unknown format {}", raw ) );

		info.m_format = static_cast< ThumbnailFormat >( raw );
	}
	info.m_pixel_data = std::move( pixels );

	return info;
}

} // namespace idhan::ipc
