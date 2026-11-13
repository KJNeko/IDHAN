#include "identifyMime.hpp"

#include <fcntl.h>

#include <cstring>
#include <format>

#include "MimeIDs.hpp"
#include "MimeReader.hpp"
#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "modules/CallInput.hpp"
#include "prescan.hpp"
#include "refineMime.hpp"

namespace idhan::mime
{

// dup() would retain write access; reopen read-only before passing the file to the module.
static runtime::Task< std::expected< std::shared_ptr< const modules::CallInput >, std::string > > readOnlyInput(
	const int descriptor,
	const std::size_t size )
{
	const int reopened { co_await IOUring::getInstance().openFile(
		std::format( "/proc/self/fd/{}", descriptor ), O_RDONLY | O_CLOEXEC, 0 ) };
	if ( reopened < 0 )
	{
		co_return std::unexpected(
			std::format( "could not reopen descriptor read-only: {}", std::strerror( -reopened ) ) );
	}

	auto input { modules::CallInput::forFd( ipc::UniqueFd { reopened }, size ) };
	co_return std::make_shared< const modules::CallInput >( std::move( input ) );
}

runtime::Task< MimeID > identifyMime(
	const std::span< const std::byte > bytes,
	const std::string_view filename,
	const std::optional< int > descriptor )
{
	MimeID mime_id { co_await prescanMime( MimeReader { bytes } ) };

	if ( mime_id != mime_ids::UNKNOWN )
	{
		std::expected< std::shared_ptr< const modules::CallInput >, std::string > input_e {};
		if ( descriptor.has_value() )
		{
			input_e = co_await readOnlyInput( descriptor.value(), bytes.size() );
		}
		else
		{
			input_e = modules::CallInput::sharedForBytes( bytes );
		}

		if ( input_e.has_value() )
			mime_id = co_await specializeMimeID( mime_id, std::move( input_e.value() ), filename );
		else
			log::warn( "Could not stage bytes to specialize mime id {}: {}", mime_id, input_e.error() );
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
