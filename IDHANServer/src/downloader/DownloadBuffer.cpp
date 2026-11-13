#include "DownloadBuffer.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/coro/CurrentExecutor.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <system_error>

namespace idhan::downloader
{

static std::filesystem::path downloadTemporaryDirectory()
{
	for ( const auto* variable : { "TMPDIR", "TMP", "TEMP" } )
	{
		const auto* value { std::getenv( variable ) };
		if ( value != nullptr && value[ 0 ] != '\0' ) return value;
	}

	return "/tmp";
}

DownloadBuffer::DownloadBuffer( IOUring& io ) : m_io( io )
{}

DownloadBuffer::~DownloadBuffer()
{
	scheduleDiscard();
}

runtime::Task< std::expected< void, std::string > > DownloadBuffer::open()
{
	if ( m_file ) co_return std::expected< void, std::string > {};

	m_executor = folly::getKeepAliveToken( co_await folly::coro::co_current_executor );
	const auto directory { downloadTemporaryDirectory() };
	int descriptor { co_await m_io.openFile( directory, O_TMPFILE | O_RDWR | O_CLOEXEC, 0600 ) };
	if ( descriptor >= 0 )
	{
		m_file = folly::File { descriptor, true };
		co_return std::expected< void, std::string > {};
	}

	if ( descriptor != -EOPNOTSUPP && descriptor != -EINVAL && descriptor != -EISDIR )
		co_return std::unexpected( std::string { std::strerror( -descriptor ) } );

	const auto path {
		directory / ( "idhan-download-" + boost::uuids::to_string( boost::uuids::random_generator {}() ) )
	};
	descriptor = co_await m_io.openFile( path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600 );
	if ( descriptor < 0 ) co_return std::unexpected( std::string { std::strerror( -descriptor ) } );

	m_file = folly::File { descriptor, true };
	const int removed { co_await m_io.removeFile( path ) };
	if ( removed < 0 )
	{
		co_await discard();
		co_return std::unexpected( std::string { std::strerror( -removed ) } );
	}

	co_return std::expected< void, std::string > {};
}

std::optional< int > DownloadBuffer::descriptor() const
{
	if ( !m_file ) return std::nullopt;

	return m_file.fd();
}

runtime::Task< std::expected< void, std::string > > DownloadBuffer::write( const std::span< const std::byte > bytes )
{
	if ( m_discarded ) co_return std::unexpected( "The download was discarded" );

	if ( !m_error.empty() ) co_return std::unexpected( m_error );

	if ( bytes.size() > static_cast< std::size_t >( std::numeric_limits< off_t >::max() ) - m_size )
		co_return std::unexpected( "Download exceeds the file offset range" );

	try
	{
		const auto opened_e { co_await open() };
		if ( !opened_e.has_value() ) co_return std::unexpected( opened_e.error() );

		co_await m_io.write( static_cast< IOUring::NativeHandle >( m_file.fd() ), bytes, m_size );
		m_size += bytes.size();
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		// A failing backend may already have written part of the chunk; never finalize it.
		m_error = error.what();
		co_return std::unexpected( m_error );
	}
}

runtime::Task< std::expected< std::vector< std::byte >, std::string > > DownloadBuffer::read( const std::size_t size )
{
	if ( m_discarded ) co_return std::unexpected( "The download was discarded" );

	if ( !m_error.empty() ) co_return std::unexpected( m_error );

	if ( size != m_size ) co_return std::unexpected( "Downloaded file size does not match received bytes" );

	try
	{
		std::vector< std::byte > bytes {};
		bytes.reserve( size );
		constexpr std::size_t block_size { 8 * 1024 * 1024 };
		while ( bytes.size() < size )
		{
			const auto want { std::min( block_size, size - bytes.size() ) };
			const auto chunk {
				co_await m_io.read( static_cast< IOUring::NativeHandle >( m_file.fd() ), bytes.size(), want )
			};
			if ( chunk.size() != want ) co_return std::unexpected( "Downloaded file ended before all received bytes" );

			bytes.insert( bytes.end(), chunk.begin(), chunk.end() );
		}
		co_return bytes;
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

runtime::Task< int > DownloadBuffer::closeFile( IOUring& io, folly::File file )
{
	co_return co_await io.closeFile( static_cast< IOUring::NativeHandle >( file.release() ) );
}

void DownloadBuffer::reportClose( folly::Try< int >&& result )
{
	if ( result.hasException() )
	{
		spdlog::error( "Closing download buffer: {}", result.exception().what() );
		return;
	}

	if ( result.value() < 0 ) spdlog::error( "Closing download buffer: {}", std::strerror( -result.value() ) );
}

void DownloadBuffer::scheduleDiscard() noexcept
{
	m_discarded = true;
	if ( !m_file ) return;

	try
	{
		folly::coro::co_withExecutor( std::move( m_executor ), closeFile( m_io, std::move( m_file ) ) )
			.start( reportClose );
	}
	catch ( const std::exception& error )
	{
		spdlog::error( "Scheduling download buffer close: {}", error.what() );
	}
}

runtime::Task< void > DownloadBuffer::discard()
{
	m_discarded = true;
	if ( !m_file ) co_return;

	const int result { co_await closeFile( m_io, std::move( m_file ) ) };
	m_executor = {};
	if ( result < 0 ) throw std::system_error( -result, std::generic_category(), "Closing download buffer" );
}

} // namespace idhan::downloader
