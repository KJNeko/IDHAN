#include "identifyMime.hpp"

#include "MimeIDs.hpp"
#include "MimeReader.hpp"
#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "modules/CallInput.hpp"
#include "prescan.hpp"
#include "refineMime.hpp"

namespace idhan::mime
{

IDHANTask< MimeID > identifyMime( const std::span< const std::byte > bytes, const std::string_view filename )
{
	auto mime_id { co_await prescanMime( MimeReader { bytes } ) };

	if ( mime_id != mime_ids::UNKNOWN )
	{
		auto input { modules::CallInput::sharedForBytes( bytes ) };
		if ( input )
			mime_id = co_await specializeMimeID( mime_id, std::move( *input ), filename );
		else
			log::warn( "Could not stage bytes to specialize mime id {}: {}", mime_id, input.error() );
	}

	co_return mime_id;
}

IDHANTask< MimeID > identifyMimeForPath( const std::filesystem::path& path )
{
	auto file { std::make_shared< FileIOUring >( path ) };
	auto mime_id { co_await prescanMime( MimeReader { std::move( file ) } ) };
	mime_id = co_await specializeMimeIDForPath( mime_id, path );

	co_return mime_id;
}

} // namespace idhan::mime
