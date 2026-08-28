#include "AnimationManifest.hpp"

#include <json/reader.h>
#include <json/value.h>

#include <memory>

#include "logging/format_ns.hpp"

static std::expected< std::vector< UgoiraFrame >, idhan::ModuleError > framesFromArray( const Json::Value& array )
{
	std::vector< UgoiraFrame > frames {};
	frames.reserve( array.size() );

	for ( Json::ArrayIndex index = 0; index < array.size(); ++index )
	{
		const auto& entry { array[ index ] };

		if ( !entry.isObject() )
			return std::unexpected(
				idhan::ModuleError { format_ns::format( "animation.json frame {} is not an object", index ) } );

		const auto& file { entry[ "file" ] };
		if ( !file.isString() )
			return std::unexpected(
				idhan::ModuleError { format_ns::format( "animation.json frame {} has no \'file\' string", index ) } );

		UgoiraFrame frame {};
		frame.m_file = file.asString();

		if ( const auto& delay { entry[ "delay" ] }; delay.isIntegral() ) frame.m_delay_ms = delay.asInt();

		if ( frame.m_delay_ms <= 0 ) frame.m_delay_ms = UGOIRA_DEFAULT_FRAME_DELAY_MS;

		frames.emplace_back( std::move( frame ) );
	}

	return std::move( frames );
}

std::expected< std::vector< UgoiraFrame >, idhan::ModuleError > parseAnimationManifest(
	const std::span< const std::byte > bytes )
{
	if ( bytes.empty() ) return std::unexpected( idhan::ModuleError { "animation.json is empty" } );

	const std::unique_ptr< Json::CharReader > reader { Json::CharReaderBuilder {}.newCharReader() };

	const auto* const begin { reinterpret_cast< const char* >( bytes.data() ) };

	Json::Value root {};
	std::string errors {};

	if ( !reader->parse( begin, begin + bytes.size(), &root, &errors ) )
		return std::unexpected(
			idhan::ModuleError { format_ns::format( "animation.json is not valid json: {}", errors ) } );

	// gallery-dl writes the bare array; the Pixiv-shaped manifest wraps it in an object.
	const auto& array { root.isArray() ? root : root[ "frames" ] };

	if ( !array.isArray() )
		return std::unexpected(
			idhan::ModuleError { "animation.json carries neither an array nor a \'frames\' array" } );

	auto frames { framesFromArray( array ) };
	if ( !frames ) return std::unexpected( frames.error() );

	if ( frames->empty() ) return std::unexpected( idhan::ModuleError { "animation.json lists no frames" } );

	return std::move( frames );
}
