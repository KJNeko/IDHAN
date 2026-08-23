#include "ipc/Protocol.hpp"

#include <exception>
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

	if ( !json[ field::INDEX ].isUInt64() )
		return std::unexpected( std::string { "manifest entry has no unsigned index" } );

	if ( !json[ field::NAME ].isString() ) return std::unexpected( std::string { "manifest entry has no name" } );

	if ( !json[ field::TYPE ].isUInt() )
		return std::unexpected( std::string { "manifest entry has no unsigned type" } );

	if ( !json[ field::VERSION ].isObject() || !json[ field::VERSION ][ field::MAJOR ].isUInt64()
	     || !json[ field::VERSION ][ field::MINOR ].isUInt64() || !json[ field::VERSION ][ field::PATCH ].isUInt64() )
		return std::unexpected( std::string { "manifest entry has an invalid version" } );

	if ( !json[ field::THREAD_SAFE ].isBool() )
		return std::unexpected( std::string { "manifest entry has no boolean thread_safe flag" } );

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

	constexpr ModuleType known_module_flags {
		ModuleTypeFlags::METADATA | ModuleTypeFlags::THUMBNAILER | ModuleTypeFlags::GENERATOR
		| ModuleTypeFlags::EMBEDDING | ModuleTypeFlags::MIME_PARSE
	};
	if ( entry.type == 0 || ( entry.type & ~known_module_flags ) != 0 )
		return std::unexpected( std::format( "manifest entry has unsupported module flags {}", entry.type ) );

	if ( json.isMember( field::RSS_CEILING_MB ) )
	{
		if ( !json[ field::RSS_CEILING_MB ].isUInt64() )
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
		if ( !json[ field::DIMENSIONS ].isUInt() )
			return std::unexpected( std::string { "manifest entry has a non-integral dimensions" } );

		entry.dimensions = static_cast< std::uint32_t >( json[ field::DIMENSIONS ].asUInt() );
	}

	if ( json.isMember( field::SUPPORTS_TEXT ) )
	{
		if ( !json[ field::SUPPORTS_TEXT ].isBool() )
			return std::unexpected( std::string { "manifest entry has a non-boolean supports_text" } );
		entry.supports_text = json[ field::SUPPORTS_TEXT ].asBool();
	}

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
		if ( !mime.isInt() )
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

		signature += std::format(
			"|{}|{}|{}|{}|{}|{}",
			entry.thread_safe,
			toWire( entry.residency ),
			entry.rss_ceiling_mb,
			entry.model_name,
			entry.dimensions,
			entry.supports_text );

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
				value[ "has_audio" ] = metadata.has_audio;
				value[ "width" ] = metadata.width;
				value[ "height" ] = metadata.height;
				value[ "bitrate" ] = metadata.bitrate_bps;
				value[ "duration" ] = metadata.duration_s;
				value[ "fps" ] = metadata.fps;
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
				value[ "duration" ] = metadata.duration_s;
				value[ "loops" ] = metadata.loops;
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoArchive > )
			{
				tag = MetadataVariant::ARCHIVE;

				Json::Value records { Json::arrayValue };
				for ( const auto& contained : metadata.contained_records )
				{
					Json::Value entry { Json::objectValue };
					entry[ "hash" ] = crypto::toHex( contained.hash );
					entry[ "path" ] = contained.path;
					records.append( std::move( entry ) );
				}

				value[ "contained_records" ] = records;
				value[ "size" ] = static_cast< Json::UInt64 >( metadata.m_size );
				value[ "encrypted" ] = metadata.encrypted;
			}
			else if constexpr ( std::is_same_v< T, MetadataInfoAudio > )
			{
				tag = MetadataVariant::AUDIO;
				value[ "duration" ] = metadata.duration_s;
				value[ "bitrate" ] = metadata.bitrate_bps;
				value[ "channels" ] = static_cast< Json::UInt >( metadata.channels );
				value[ "sample_rate" ] = metadata.sample_rate;
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
	try
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
					.has_audio = value[ "has_audio" ].asBool(),
					.width = value[ "width" ].asInt(),
					.height = value[ "height" ].asInt(),
					.bitrate_bps = value[ "bitrate" ].asInt(),
					.duration_s = value[ "duration" ].asDouble(),
					.fps = value[ "fps" ].asDouble()
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
					.duration_s = value[ "duration" ].asDouble(),
					.loops = value[ "loops" ].asBool()
				};
				break;
			case MetadataVariant::AUDIO:
				info.m_metadata = MetadataInfoAudio {
					.duration_s = value[ "duration" ].asDouble(),
					.bitrate_bps = value[ "bitrate" ].asInt(),
					.channels = static_cast< std::uint8_t >( value[ "channels" ].asUInt() ),
					.sample_rate = value[ "sample_rate" ].asInt()
				};
				break;
			case MetadataVariant::ARCHIVE:
				{
					MetadataInfoArchive archive {};

					const auto& records { value[ "contained_records" ] };
					if ( !records.isArray() )
						return std::unexpected( std::string { "archive metadata has no record array" } );

					for ( const auto& contained : records )
					{
						if ( !contained.isObject() )
							return std::unexpected( std::string { "archive metadata has a non-object entry" } );

						if ( !contained[ "hash" ].isString() )
							return std::unexpected( std::string { "archive metadata entry has a non-string hash" } );

						const auto hex { contained[ "hash" ].asString() };
						if ( hex.size() != ( 256 / 8 ) * 2 )
							return std::unexpected(
								std::format( "archive metadata hash '{}' is not a sha256 hex string", hex ) );

						if ( !contained[ "path" ].isString() )
							return std::unexpected(
								std::format( "archive metadata entry '{}' has a non-string path", hex ) );

						archive.contained_records.emplace_back(
							crypto::fromHex( hex ), contained[ "path" ].asString() );
					}

					archive.m_size = static_cast< std::size_t >( value[ "size" ].asUInt64() );
					archive.encrypted = value[ "encrypted" ].asBool();

					info.m_metadata = std::move( archive );
					break;
				}
		}

		return info;
	}
	catch ( const std::exception& error )
	{
		return std::unexpected( std::format( "metadata has invalid field types: {}", error.what() ) );
	}
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
	if ( !json[ field::WIDTH ].isUInt64() )
		return std::unexpected( std::string { "thumbnail header has no unsigned width" } );
	if ( !json[ field::HEIGHT ].isUInt64() )
		return std::unexpected( std::string { "thumbnail header has no unsigned height" } );
	if ( !json[ field::CACHE_THUMBNAIL ].isBool() )
		return std::unexpected( std::string { "thumbnail header has no boolean cache flag" } );

	const auto width { json[ field::WIDTH ].asUInt64() };
	if ( width == 0 ) return std::unexpected( std::string { "thumbnail header has a zero width" } );
	if ( width > std::numeric_limits< std::size_t >::max() )
		return std::unexpected( std::string { "thumbnail header width is too large" } );

	const auto height { json[ field::HEIGHT ].asUInt64() };
	if ( height == 0 ) return std::unexpected( std::string { "thumbnail header has a zero height" } );
	if ( height > std::numeric_limits< std::size_t >::max() )
		return std::unexpected( std::string { "thumbnail header height is too large" } );

	ThumbnailInfo info {};
	info.width = static_cast< std::size_t >( width );
	info.height = static_cast< std::size_t >( height );
	info.cache_thumbnail = json[ field::CACHE_THUMBNAIL ].asBool();

	// An older worker sends no format field; RGB is what every module produced before ANIMATED existed.
	if ( const auto& format { json[ field::FORMAT ] }; !format.isNull() )
	{
		if ( !format.isUInt() ) return std::unexpected( std::string { "thumbnail header has an invalid format" } );
		const auto raw { format.asUInt() };
		if ( raw > static_cast< std::uint8_t >( ThumbnailFormat::ANIMATED ) )
			return std::unexpected( std::format( "thumbnail header carried unknown format {}", raw ) );

		info.m_format = static_cast< ThumbnailFormat >( raw );
	}

	if ( info.m_format == ThumbnailFormat::RGB )
	{
		if ( info.width > std::numeric_limits< std::size_t >::max() / info.height / 3 )
			return std::unexpected( std::string { "thumbnail dimensions overflow the RGB byte count" } );
		const auto expected { info.width * info.height * 3 };
		if ( pixels.size() != expected )
			return std::unexpected( std::format( "RGB thumbnail has {} bytes; expected {}", pixels.size(), expected ) );
	}
	else if ( pixels.empty() )
	{
		return std::unexpected( std::string { "animated thumbnail has no payload" } );
	}
	info.m_pixel_data = std::move( pixels );

	return info;
}

} // namespace idhan::ipc
