#include "imports/BufferedImportSink.hpp"

#include "HostCallback.hpp"

namespace idhan::downloader
{
BufferedImportSink::BufferedImportSink( std::unique_ptr< ImportSink > sink, folly::Executor& executor ) :
  m_state( std::make_shared< State >() ),
  m_executor( executor )
{
	m_state->sink = std::move( sink );
	m_state->drained.post();
}

bool BufferedImportSink::canWrite( const std::size_t bytes ) const
{
	const std::scoped_lock lock { m_state->mutex };
	const bool ready {
		m_state->aborted || !m_state->error.empty() || m_state->bytes == 0 || m_state->bytes + bytes <= 1024 * 1024
	};
	if ( !ready ) m_state->waiting = true;

	return ready;
}

void BufferedImportSink::setResumeCallback( std::function< void() > callback )
{
	const std::scoped_lock lock { m_state->mutex };
	m_state->resume = std::move( callback );
}

std::expected< void, std::string > BufferedImportSink::enqueue( const std::span< const std::byte > bytes )
{
	const std::scoped_lock lock { m_state->mutex };
	if ( m_state->aborted ) return std::unexpected( "Download discarded" );

	if ( !m_state->error.empty() ) return std::unexpected( m_state->error );

	m_state->chunks.emplace_back( bytes.begin(), bytes.end() );
	m_state->bytes += bytes.size();
	if ( !m_state->running )
	{
		m_state->drained.reset();
		m_state->running = true;
		try
		{
			folly::coro::co_withExecutor( &m_executor, drain( m_state ) ).start( reportHostTaskResult );
		}
		catch ( const std::exception& error )
		{
			m_state->running = false;
			m_state->error = error.what();
			m_state->drained.post();
			return std::unexpected( m_state->error );
		}
	}
	return {};
}

folly::coro::Task< std::expected< void, std::string > > BufferedImportSink::writeChunk(
	ImportSink& sink,
	const std::span< const std::byte > bytes )
{
	// Invoke the host hook inside a coroutine so the adapter catches task-creation errors too.
	co_return co_await sink.write( bytes );
}

folly::coro::Task< void > BufferedImportSink::drain( std::shared_ptr< State > state )
{
	for ( ;; )
	{
		std::vector< std::byte > chunk;
		bool aborted {};
		{
			const std::scoped_lock lock { state->mutex };
			aborted = state->aborted;
			if ( aborted || !state->error.empty() || state->chunks.empty() )
			{
				state->chunks.clear();
				state->bytes = 0;
				if ( !aborted )
				{
					state->running = false;
					state->drained.post();
					co_return;
				}
			}
			else
			{
				chunk = std::move( state->chunks.front() );
				state->chunks.pop_front();
			}
		}
		if ( aborted )
		{
			(void)hostCallback( [ & ] { state->sink->abort(); } );
			const std::scoped_lock lock { state->mutex };
			state->running = false;
			state->drained.post();
			co_return;
		}

		std::expected< void, std::string > written_e { co_await hostOperation( writeChunk( *state->sink, chunk ) ) };

		std::function< void() > resume;
		{
			const std::scoped_lock lock { state->mutex };
			state->bytes -= chunk.size();
			if ( !written_e.has_value() ) state->error = written_e.error();

			if ( state->waiting && ( state->bytes < 512 * 1024 || !state->error.empty() ) )
			{
				state->waiting = false;
				resume = state->resume;
			}
		}
		if ( resume ) (void)hostCallback( resume );
	}
}

folly::coro::Task< std::expected< void, std::string > > BufferedImportSink::waitForDrain()
{
	for ( ;; )
	{
		{
			const std::scoped_lock lock { m_state->mutex };
			if ( !m_state->running ) break;
		}
		co_await m_state->drained;
	}
	{
		const std::scoped_lock lock { m_state->mutex };
		if ( m_state->aborted ) co_return std::unexpected( "Download discarded" );

		if ( !m_state->error.empty() ) co_return std::unexpected( m_state->error );
	}
	co_return std::expected< void, std::string > {};
}

folly::coro::Task< std::expected< void, std::string > > BufferedImportSink::write(
	const std::span< const std::byte > bytes )
{
	const auto queued_e { enqueue( bytes ) };
	if ( !queued_e.has_value() ) co_return std::unexpected( queued_e.error() );

	co_return co_await waitForDrain();
}

folly::coro::Task< std::expected< ImportResult, std::string > > BufferedImportSink::finish( ImportMetadata metadata )
{
	const auto drained_e { co_await waitForDrain() };
	if ( !drained_e.has_value() ) co_return std::unexpected( drained_e.error() );

	co_return co_await m_state->sink->finish( std::move( metadata ) );
}

void BufferedImportSink::abort()
{
	const std::scoped_lock lock { m_state->mutex };
	m_state->aborted = true;
	m_state->resume = {};
	if ( !m_state->running )
	{
		m_state->drained.reset();
		m_state->running = true;
		try
		{
			folly::coro::co_withExecutor( &m_executor, drain( m_state ) ).start( reportHostTaskResult );
		}
		catch ( const std::exception& )
		{
			m_state->running = false;
			m_state->drained.post();
		}
	}
}
} // namespace idhan::downloader
