#include "http/IoPool.hpp"

#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace idhan::downloader
{

static std::once_flag curl_initialised {};
static bool curl_http3 {};
static bool curl_http2 {};

void IoPool::initialiseCurl()
{
	std::call_once(
		curl_initialised,
		[]
		{
			const auto code { curl_global_init( CURL_GLOBAL_DEFAULT ) };

			if ( code != CURLE_OK )
			{
				spdlog::error( "downloader http: curl_global_init failed: {}", curl_easy_strerror( code ) );
				return;
			}

			const auto* version { curl_version_info( CURLVERSION_NOW ) };

			if ( version == nullptr ) return;

			curl_http2 = ( version->features & CURL_VERSION_HTTP2 ) != 0;
			curl_http3 = ( version->features & CURL_VERSION_HTTP3 ) != 0;
			spdlog::info(
				"downloader http: libcurl {} (HTTP/2 {}, HTTP/3 {})",
				version->version,
				curl_http2 ? "yes" : "no",
				curl_http3 ? "yes" : "no" );
		} );
}

bool IoPool::supportsHttp3()
{
	initialiseCurl();
	return curl_http3;
}

bool IoPool::supportsHttp2()
{
	initialiseCurl();
	return curl_http2;
}

IoThread::IoThread()
{
	IoPool::initialiseCurl();

	m_epoll_fd = ::epoll_create1( EPOLL_CLOEXEC );

	if ( m_epoll_fd < 0 ) throw std::runtime_error( "downloader http: epoll_create1 failed" );

	m_wakeup_fd = ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );

	if ( m_wakeup_fd < 0 )
	{
		::close( m_epoll_fd );
		throw std::runtime_error( "downloader http: eventfd failed" );
	}

	m_wakeup_tag = std::make_unique< EventTag >();
	m_wakeup_tag->kind = EventKind::WAKEUP;

	epoll_event event {};
	event.events = EPOLLIN;
	event.data.ptr = m_wakeup_tag.get();

	if ( ::epoll_ctl( m_epoll_fd, EPOLL_CTL_ADD, m_wakeup_fd, &event ) != 0 )
	{
		::close( m_wakeup_fd );
		::close( m_epoll_fd );
		throw std::runtime_error( "downloader http: epoll_ctl on the wakeup descriptor failed" );
	}

	m_deadline_fd = ::timerfd_create( CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK );

	if ( m_deadline_fd < 0 )
	{
		::close( m_wakeup_fd );
		::close( m_epoll_fd );
		throw std::runtime_error( "downloader http: timerfd_create for the deadline queue failed" );
	}

	m_deadline_tag = std::make_unique< EventTag >();
	m_deadline_tag->kind = EventKind::DEADLINE;

	epoll_event deadline_event {};
	deadline_event.events = EPOLLIN;
	deadline_event.data.ptr = m_deadline_tag.get();

	if ( ::epoll_ctl( m_epoll_fd, EPOLL_CTL_ADD, m_deadline_fd, &deadline_event ) != 0 )
	{
		::close( m_deadline_fd );
		::close( m_wakeup_fd );
		::close( m_epoll_fd );
		throw std::runtime_error( "downloader http: epoll_ctl on the deadline descriptor failed" );
	}

	m_thread = std::jthread( [ this ] { run(); } );
}

IoThread::~IoThread()
{
	stop();

	if ( m_thread.joinable() ) m_thread.join();
	if ( m_deadline_fd >= 0 ) ::close( m_deadline_fd );
	if ( m_wakeup_fd >= 0 ) ::close( m_wakeup_fd );
	if ( m_epoll_fd >= 0 ) ::close( m_epoll_fd );
}

void IoThread::stop()
{
	if ( !m_running.exchange( false ) ) return;

	const std::uint64_t one { 1 };
	[[maybe_unused]] const auto written { ::write( m_wakeup_fd, &one, sizeof( one ) ) };
}

void IoThread::post( Task task )
{
	{
		const std::scoped_lock lock { m_tasks_mutex };
		m_tasks.emplace_back( std::move( task ) );
	}

	const std::uint64_t one { 1 };
	[[maybe_unused]] const auto written { ::write( m_wakeup_fd, &one, sizeof( one ) ) };
}

void IoThread::drainTasks()
{
	std::deque< Task > taken {};

	{
		const std::scoped_lock lock { m_tasks_mutex };
		taken.swap( m_tasks );
	}

	for ( Task& task : taken ) task();
}

void IoThread::postAfter( const std::chrono::steady_clock::duration delay, Task task )
{
	const auto due { std::chrono::steady_clock::now() + delay };

	post(
		[ this, due, moved = std::move( task ) ]() mutable
		{
			m_timers.emplace( due, std::move( moved ) );
			armDeadline();
		} );
}

void IoThread::armDeadline()
{
	itimerspec timer {};

	if ( !m_timers.empty() )
	{
		const auto remaining { std::max(
			std::chrono::steady_clock::duration { 1 }, m_timers.begin()->first - std::chrono::steady_clock::now() ) };
		const auto nanoseconds { std::chrono::duration_cast< std::chrono::nanoseconds >( remaining ).count() };
		timer.it_value.tv_sec = nanoseconds / 1000000000;
		timer.it_value.tv_nsec = nanoseconds % 1000000000;
	}

	::timerfd_settime( m_deadline_fd, 0, &timer, nullptr );
}

void IoThread::runDueTimers()
{
	const auto now { std::chrono::steady_clock::now() };
	std::vector< Task > due {};

	while ( !m_timers.empty() && m_timers.begin()->first <= now )
	{
		due.emplace_back( std::move( m_timers.begin()->second ) );
		m_timers.erase( m_timers.begin() );
	}

	for ( Task& task : due ) task();

	armDeadline();
}

int IoThread::socketCallback( CURL*, const curl_socket_t socket, const int what, void* user, void* socket_user )
{
	auto* registration { static_cast< Registration* >( user ) };
	IoThread* thread { registration->thread };
	auto* record { static_cast< SocketEvent* >( socket_user ) };

	if ( what == CURL_POLL_REMOVE )
	{
		if ( record == nullptr ) return 0;

		::epoll_ctl( thread->m_epoll_fd, EPOLL_CTL_DEL, socket, nullptr );
		curl_multi_assign( registration->source->multi(), socket, nullptr );
		record->detached = true;

		const auto owned {
			std::ranges::find_if( thread->m_sockets, [ record ]( const auto& held ) { return held.get() == record; } )
		};

		if ( owned != thread->m_sockets.end() )
		{
			thread->m_retired_sockets.emplace_back( std::move( *owned ) );
			thread->m_sockets.erase( owned );
		}

		return 0;
	}

	std::uint32_t events {};

	if ( what == CURL_POLL_IN || what == CURL_POLL_INOUT ) events |= EPOLLIN;
	if ( what == CURL_POLL_OUT || what == CURL_POLL_INOUT ) events |= EPOLLOUT;

	epoll_event event {};
	event.events = events;

	if ( record == nullptr )
	{
		auto owned { std::make_unique< SocketEvent >() };
		owned->kind = EventKind::SOCKET;
		owned->registration = registration;
		owned->socket = socket;
		record = owned.get();
		thread->m_sockets.emplace_back( std::move( owned ) );
		curl_multi_assign( registration->source->multi(), socket, record );
		event.data.ptr = record;

		if ( ::epoll_ctl( thread->m_epoll_fd, EPOLL_CTL_ADD, socket, &event ) != 0 )
			spdlog::error( "downloader http: epoll_ctl add on a transfer socket failed: {}", std::strerror( errno ) );

		return 0;
	}

	event.data.ptr = record;
	::epoll_ctl( thread->m_epoll_fd, EPOLL_CTL_MOD, socket, &event );
	return 0;
}

int IoThread::timerCallback( CURLM*, const long timeout_ms, void* user )
{
	auto* registration { static_cast< Registration* >( user ) };

	if ( registration->detached || registration->timer_fd < 0 ) return 0;

	itimerspec timer {};

	if ( timeout_ms >= 0 )
	{
		timer.it_value.tv_sec = timeout_ms / 1000;
		timer.it_value.tv_nsec = ( timeout_ms % 1000 ) * 1000000;

		//! timerfd reads an all-zero value as "disarm", so an immediate timeout arms one nanosecond out.
		if ( timer.it_value.tv_sec == 0 && timer.it_value.tv_nsec == 0 ) timer.it_value.tv_nsec = 1;
	}

	::timerfd_settime( registration->timer_fd, 0, &timer, nullptr );
	return 0;
}

void IoThread::attach( IoSource& source )
{
	CURLM* multi { source.multi() };

	if ( multi == nullptr || m_sources.contains( &source ) ) return;

	const int timer_fd { ::timerfd_create( CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK ) };

	if ( timer_fd < 0 )
	{
		spdlog::error( "downloader http: timerfd_create failed: {}", std::strerror( errno ) );
		return;
	}

	auto registration { std::make_unique< Registration >() };
	registration->kind = EventKind::TIMER;
	registration->thread = this;
	registration->source = &source;
	registration->timer_fd = timer_fd;

	epoll_event event {};
	event.events = EPOLLIN;
	event.data.ptr = registration.get();

	if ( ::epoll_ctl( m_epoll_fd, EPOLL_CTL_ADD, timer_fd, &event ) != 0 )
	{
		spdlog::error( "downloader http: epoll_ctl on a lane timer failed: {}", std::strerror( errno ) );
		::close( timer_fd );
		return;
	}

	Registration* raw { registration.get() };
	m_sources.emplace( &source, std::move( registration ) );
	m_load.fetch_add( 1, std::memory_order_relaxed );

	curl_multi_setopt( multi, CURLMOPT_SOCKETFUNCTION, socketCallback );
	curl_multi_setopt( multi, CURLMOPT_SOCKETDATA, raw );
	curl_multi_setopt( multi, CURLMOPT_TIMERFUNCTION, timerCallback );
	curl_multi_setopt( multi, CURLMOPT_TIMERDATA, raw );
}

void IoThread::retireSocketsOf( const Registration& registration )
{
	for ( auto held = m_sockets.begin(); held != m_sockets.end(); )
	{
		if ( ( *held )->registration != &registration )
		{
			++held;
			continue;
		}

		::epoll_ctl( m_epoll_fd, EPOLL_CTL_DEL, ( *held )->socket, nullptr );
		( *held )->detached = true;
		m_retired_sockets.emplace_back( std::move( *held ) );
		held = m_sockets.erase( held );
	}
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

	retireSocketsOf( *found->second );
	::epoll_ctl( m_epoll_fd, EPOLL_CTL_DEL, found->second->timer_fd, nullptr );
	::close( found->second->timer_fd );
	found->second->timer_fd = -1;
	found->second->detached = true;
	m_retired_sources.emplace_back( std::move( found->second ) );
	m_sources.erase( found );
	m_load.fetch_sub( 1, std::memory_order_relaxed );
}

void IoThread::act( IoSource& source, const curl_socket_t socket, const int events )
{
	int running {};
	curl_multi_socket_action( source.multi(), socket, events, &running );
	source.onProgress();
}

void IoThread::run()
{
	m_thread_id.store( std::this_thread::get_id() );
	::pthread_setname_np( ::pthread_self(), "downloader-io" );

	std::array< epoll_event, 64 > events {};

	while ( m_running.load( std::memory_order_relaxed ) )
	{
		const int count { ::epoll_wait( m_epoll_fd, events.data(), static_cast< int >( events.size() ), -1 ) };

		if ( count < 0 )
		{
			if ( errno == EINTR ) continue;

			spdlog::error( "downloader http: epoll_wait failed: {}", std::strerror( errno ) );
			break;
		}

		for ( std::size_t index = 0; index < static_cast< std::size_t >( count ); ++index )
		{
			auto* tag { static_cast< EventTag* >( events[ index ].data.ptr ) };

			if ( tag == nullptr || tag->detached ) continue;

			switch ( tag->kind )
			{
				case EventKind::WAKEUP:
					{
						std::uint64_t value {};
						[[maybe_unused]] const auto read_bytes { ::read( m_wakeup_fd, &value, sizeof( value ) ) };
						drainTasks();
						break;
					}
				case EventKind::DEADLINE:
					{
						std::uint64_t expirations {};
						[[maybe_unused]] const auto read_bytes {
							::read( m_deadline_fd, &expirations, sizeof( expirations ) )
						};
						runDueTimers();
						break;
					}
				case EventKind::TIMER:
					{
						auto* registration { static_cast< Registration* >( tag ) };
						std::uint64_t expirations {};
						[[maybe_unused]] const auto read_bytes {
							::read( registration->timer_fd, &expirations, sizeof( expirations ) )
						};
						act( *registration->source, CURL_SOCKET_TIMEOUT, 0 );
						break;
					}
				case EventKind::SOCKET:
					{
						auto* socket_event { static_cast< SocketEvent* >( tag ) };

						if ( socket_event->registration->detached ) break;

						int flags {};

						if ( ( events[ index ].events & ( EPOLLIN | EPOLLHUP ) ) != 0 ) flags |= CURL_CSELECT_IN;
						if ( ( events[ index ].events & EPOLLOUT ) != 0 ) flags |= CURL_CSELECT_OUT;
						if ( ( events[ index ].events & EPOLLERR ) != 0 ) flags |= CURL_CSELECT_ERR;

						act( *socket_event->registration->source, socket_event->socket, flags );
						break;
					}
			}
		}

		m_retired_sockets.clear();
		m_retired_sources.clear();
	}

	drainTasks();
}

IoPool::IoPool( const std::size_t count )
{
	initialiseCurl();
	const std::size_t threads {
		count == 0 ? std::max< std::size_t >( 2, std::thread::hardware_concurrency() / 2 ) : count
	};
	m_threads.reserve( threads );

	for ( std::size_t index = 0; index < threads; ++index ) m_threads.emplace_back( std::make_unique< IoThread >() );
}

IoPool::~IoPool()
{
	stop();
}

void IoPool::stop()
{
	for ( auto& thread : m_threads ) thread->stop();
}

IoThread& IoPool::leastLoaded()
{
	auto* best { m_threads.front().get() };

	for ( auto& thread : m_threads )
	{
		if ( thread->load() < best->load() ) best = thread.get();
	}

	return *best;
}

} // namespace idhan::downloader
