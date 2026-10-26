#include "identifyMime.hpp"

#include <fcntl.h>

#include "MimeIDs.hpp"
#include "MimeReader.hpp"
#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "modules/CallInput.hpp"
#include "prescan.hpp"
#include "refineMime.hpp"

namespace idhan::mime
{

runtime::Task< MimeID > identifyMime( const std::span< const std::byte > bytes, const std::string_view filename )
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

runtime::Task< MimeID > identifyMimeForFd( const int fd, const std::size_t size, const std::string_view filename )
{
	const auto duplicate = [ fd ]() { return ::fcntl( fd, F_DUPFD_CLOEXEC, 0 ); };

	auto reader_fd { duplicate() };
	if ( reader_fd < 0 ) co_return mime_ids::UNKNOWN;

	auto mime_id { co_await prescanMime( MimeReader { std::make_shared< FileIOUring >( reader_fd, size ) } ) };
	if ( mime_id == mime_ids::UNKNOWN ) co_return mime_id;

	auto input_fd { duplicate() };
	if ( input_fd < 0 ) co_return mime_id;

	auto input { modules::CallInput::forFd( ipc::UniqueFd { input_fd } ) };
	if ( !input )
	{
		log::warn( "Could not stage descriptor to specialize mime id {}: {}", mime_id, input.error() );
		co_return mime_id;
	}

	co_return co_await specializeMimeID(
		mime_id, std::make_shared< const modules::CallInput >( std::move( *input ) ), filename );
}

runtime::Task< MimeID > identifyMimeForPath( const std::filesystem::path& path )
{
	auto file { std::make_shared< FileIOUring >( path ) };
	auto mime_id { co_await prescanMime( MimeReader { std::move( file ) } ) };
	mime_id = co_await specializeMimeIDForPath( mime_id, path );

	co_return mime_id;
}

} // namespace idhan::mime
