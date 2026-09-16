#include "http/IoThread.hpp"

#include <spdlog/spdlog.h>

#include <pthread.h>
#include <utility>

#include "http/IoPool.hpp"

namespace idhan::downloader
{

IoThread::SocketHandler::SocketHandler( folly::EventBase* base, const curl_socket_t fd, SourceState& source_state ) :
  folly::EventHandler( base, folly::NetworkSocket::fromFd( fd ) ),
  socket( fd ),
  owner( source_state )
{}

void IoThread::SocketHandler::handlerReady( const std::uint16_t events ) noexcept
{
	int flags {};

	if ( ( events & folly::EventHandler::READ ) != 0 ) flags |= CURL_CSELECT_IN;
	if ( ( events & folly::EventHandler::WRITE ) != 0 ) flags |= CURL_CSELECT_OUT;

	owner.thread.act( owner, socket, flags );
}

IoThread::SourceState::SourceState( folly::EventBase* base, IoThread& io_thread, IoSource& io_source ) :
  folly::AsyncTimeout( base ),
  thread( io_thread ),
  source( io_source )
{}

void IoThread::SourceState::timeoutExpired() noexcept
{
	thread.act( *this, CURL_SOCKET_TIMEOUT, 0 );
}

IoThread::IoThread()
{
	IoPool::initialiseCurl();

	m_thread = std::jthread( [ this ] { run(); } );
}

IoThread::~IoThread()
{
	stop();

	if ( m_thread.joinable() ) m_thread.join();
}

void IoThread::stop()
{
	m_event_base.terminateLoopSoon();
}

void IoThread::post( Task task )
{
	m_event_base.runInEventBaseThread( std::move( task ) );
}

void IoThread::postAfter( const std::chrono::steady_clock::duration delay, Task task )
{
	const auto ms { std::chrono::duration_cast< std::chrono::milliseconds >( delay ) };

	post( [ this, ms, moved = std::move( task ) ]() mutable
	      { m_event_base.timer().scheduleTimeoutFn( std::move( moved ), ms ); } );
}

void IoThread::scheduleCleanup()
{
	if ( m_cleanup_scheduled ) return;

	m_cleanup_scheduled = true;

	m_event_base.runInEventBaseThread(
		[ this ]
		{
			m_cleanup_scheduled = false;
			m_retired_sockets.clear();
		} );
}

int IoThread::socketCallback( CURL*, const curl_socket_t socket, const int what, void* user, void* socket_user )
{
	auto* state { static_cast< SourceState* >( user ) };
	IoThread& thread { state->thread };
	auto* handler { static_cast< SocketHandler* >( socket_user ) };

	if ( what == CURL_POLL_REMOVE )
	{
		if ( handler == nullptr ) return 0;

		handler->unregisterHandler();
		curl_multi_assign( state->source.multi(), socket, nullptr );

		if ( const auto found { state->sockets.find( socket ) }; found != state->sockets.end() )
		{
			thread.m_retired_sockets.emplace_back( std::move( found->second ) );
			state->sockets.erase( found );
		}

		thread.scheduleCleanup();
		return 0;
	}

	//! PERSIST is required: without it, folly::EventHandler unregisters itself after the first
	//! handlerReady() fire, but curl's multi-socket API expects epoll-style level-triggered
	//! monitoring that keeps reporting readiness across loop iterations until curl re-polls it.
	int events { folly::EventHandler::PERSIST };

	if ( what == CURL_POLL_IN || what == CURL_POLL_INOUT ) events |= folly::EventHandler::READ;
	if ( what == CURL_POLL_OUT || what == CURL_POLL_INOUT ) events |= folly::EventHandler::WRITE;

	if ( handler == nullptr )
	{
		auto owned { std::make_unique< SocketHandler >( &thread.m_event_base, socket, *state ) };
		handler = owned.get();
		curl_multi_assign( state->source.multi(), socket, handler );
		state->sockets.emplace( socket, std::move( owned ) );
	}

	if ( !handler->registerHandler( static_cast< std::uint16_t >( events ) ) )
		spdlog::error( "downloader http: registering a transfer socket with folly failed" );

	return 0;
}

int IoThread::timerCallback( CURLM*, const long timeout_ms, void* user )
{
	auto* state { static_cast< SourceState* >( user ) };

	if ( timeout_ms < 0 )
	{
		state->cancelTimeout();
		return 0;
	}

	state->scheduleTimeout( static_cast< std::uint32_t >( timeout_ms ) );
	return 0;
}

void IoThread::attach( IoSource& source )
{
	CURLM* multi { source.multi() };

	if ( multi == nullptr || m_sources.contains( &source ) ) return;

	auto state { std::make_unique< SourceState >( &m_event_base, *this, source ) };
	SourceState* raw { state.get() };
	m_sources.emplace( &source, std::move( state ) );
	m_load.fetch_add( 1, std::memory_order_relaxed );

	curl_multi_setopt( multi, CURLMOPT_SOCKETFUNCTION, socketCallback );
	curl_multi_setopt( multi, CURLMOPT_SOCKETDATA, raw );
	curl_multi_setopt( multi, CURLMOPT_TIMERFUNCTION, timerCallback );
	curl_multi_setopt( multi, CURLMOPT_TIMERDATA, raw );
}

void IoThread::detach( IoSource& source )
{
	const auto found { m_sources.find( &source ) };

	if ( found == m_sources.end() ) return;

	if ( CURLM* multi { source.multi() }; multi != nullptr )
	{
		curl_multi_setopt( multi, CURLMOPT_SOCKETFUNCTION, nullptr );
		curl_multi_setopt( multi, CURLMOPT_SOCKETDATA, nullptr );
		curl_multi_setopt( multi, CURLMOPT_TIMERFUNCTION, nullptr );
		curl_multi_setopt( multi, CURLMOPT_TIMERDATA, nullptr );
	}

	m_sources.erase( found );
	m_load.fetch_sub( 1, std::memory_order_relaxed );
}

void IoThread::act( SourceState& state, const curl_socket_t socket, const int events )
{
	int running {};
	curl_multi_socket_action( state.source.multi(), socket, events, &running );
	state.source.onProgress();
}

void IoThread::run()
{
	m_thread_id.store( std::this_thread::get_id() );
	::pthread_setname_np( ::pthread_self(), "downloader-io" );

	m_event_base.loopForever();
}

} // namespace idhan::downloader
