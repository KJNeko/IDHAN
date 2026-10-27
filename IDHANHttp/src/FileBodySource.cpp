#include "idhan/http/FileBodySource.hpp"

#include <folly/FileUtil.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/IOBuf.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace idhan::http
{

namespace
{
constexpr std::uint32_t read_chunk_size { 256 * 1024 };

struct ReadResult
{
	ssize_t bytes;
	int error;
};

folly::coro::Task< ReadResult > readChunk(
	const int fd,
	void* data,
	const std::size_t want,
	const std::uint64_t offset )
{
	const auto bytes { folly::preadFull( fd, data, want, static_cast< off_t >( offset ) ) };
	// errno must be captured on the same thread as the syscall: the awaiting coroutine may
	// resume on a different thread pool worker once this task completes.
	co_return ReadResult { bytes, errno };
}
} // namespace

FileBodySource::FileBodySource(
	proxygen::HTTPMessage message,
	folly::File file,
	const std::uint64_t offset,
	const std::uint64_t length ) :
  m_message( std::move( message ) ),
  m_file( std::move( file ) ),
  m_offset( offset ),
  m_remaining( length )
{}

folly::coro::Task< proxygen::coro::HTTPHeaderEvent > FileBodySource::readHeaderEvent()
{
	auto message { std::make_unique< proxygen::HTTPMessage >( std::move( *m_message ) ) };
	m_message.reset();
	proxygen::coro::HTTPHeaderEvent event { std::move( message ), m_remaining == 0 };
	auto guard { folly::makeGuard( lifetime( event ) ) };
	co_return event;
}

folly::coro::Task< proxygen::coro::HTTPBodyEvent > FileBodySource::readBodyEvent( const std::uint32_t max )
{
	if ( m_stopped.load() || m_remaining == 0 )
	{
		proxygen::coro::HTTPBodyEvent event { std::unique_ptr< folly::IOBuf > {}, true };
		auto guard { folly::makeGuard( lifetime( event ) ) };
		co_return event;
	}

	const auto want { std::min< std::uint64_t >( { max, read_chunk_size, m_remaining } ) };

	auto buffer { folly::IOBuf::create( want ) };
	auto* const data { buffer->writableTail() };

	const auto result { co_await folly::coro::co_withExecutor(
		folly::getGlobalCPUExecutor(), readChunk( m_file.fd(), data, want, m_offset ) ) };

	if ( result.bytes < 0 )
		co_yield folly::coro::co_error(
			proxygen::coro::HTTPError( proxygen::coro::HTTPErrorCode::INTERNAL_ERROR, std::strerror( result.error ) ) );

	if ( result.bytes == 0 )
		co_yield folly::coro::co_error(
			proxygen::coro::HTTPError(
				proxygen::coro::HTTPErrorCode::INTERNAL_ERROR, "File shrank while its bytes were being served" ) );

	const auto read { static_cast< std::size_t >( result.bytes ) };
	buffer->append( read );
	m_offset += read;
	m_remaining -= read;

	proxygen::coro::HTTPBodyEvent event { std::move( buffer ), m_remaining == 0 };
	auto guard { folly::makeGuard( lifetime( event ) ) };
	co_return event;
}

void FileBodySource::stopReading( folly::Optional< const proxygen::coro::HTTPErrorCode > ) noexcept
{
	m_stopped.store( true );
}

} // namespace idhan::http
