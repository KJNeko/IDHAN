#include "idhan/db/DBReactor.hpp"

#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#include "idhan/db/DBConnection.hpp"

namespace idhan::db
{
static std::uint32_t epollEvents( SocketInterest interest )
{
	std::uint32_t events { EPOLLONESHOT | EPOLLRDHUP };
	if ( interest != SocketInterest::write ) events |= EPOLLIN;
	if ( interest != SocketInterest::read ) events |= EPOLLOUT;
	return events;
}

static void epollControl( int fd, int operation, int socket, epoll_event* event )
{
	if ( epoll_ctl( fd, operation, socket, event ) == -1 )
		throw std::system_error( errno, std::generic_category(), "epoll_ctl for PostgreSQL socket failed" );
}

DBReactor::DBReactor( const std::size_t workers )
{
	if ( workers == 0 ) throw std::invalid_argument( "Database reactor requires at least one worker" );
	m_epoll_fd = epoll_create1( EPOLL_CLOEXEC );
	if ( m_epoll_fd == -1 ) throw std::system_error( errno, std::generic_category(), "epoll_create1 failed" );
	m_wake_fd = eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
	if ( m_wake_fd == -1 )
	{
		const auto error { errno };
		::close( m_epoll_fd );
		throw std::system_error( error, std::generic_category(), "eventfd failed" );
	}
	try
	{
		epoll_event event {};
		event.events = EPOLLIN;
		event.data.ptr = nullptr;
		epollControl( m_epoll_fd, EPOLL_CTL_ADD, m_wake_fd, &event );
	}
	catch ( ... )
	{
		::close( m_wake_fd );
		::close( m_epoll_fd );
		throw;
	}
	m_workers = std::make_unique< folly::CPUThreadPoolExecutor >(
		workers, std::make_shared< folly::NamedThreadFactory >( "idhan-db-pump-" ) );
}

DBReactor::~DBReactor()
{
	stop();
	if ( m_wake_fd >= 0 ) ::close( m_wake_fd );
	if ( m_epoll_fd >= 0 ) ::close( m_epoll_fd );
}

void DBReactor::notify() noexcept
{
	const std::uint64_t one { 1 };
	while ( ::write( m_wake_fd, &one, sizeof( one ) ) == -1 && errno == EINTR )
	{}
}

void DBReactor::request( const std::shared_ptr< Watch >& watch )
{
	watch->pending.store( true, std::memory_order_release );
	if ( watch->scheduled.exchange( true, std::memory_order_acq_rel ) ) return;
	m_workers->add(
		[ watch ]
		{
			while ( true )
			{
				watch->pending.store( false, std::memory_order_release );
				watch->connection->pump();

				if ( watch->pending.exchange( false, std::memory_order_acq_rel ) ) continue;

				watch->scheduled.store( false, std::memory_order_release );

				if ( !watch->pending.exchange( false, std::memory_order_acq_rel ) ) break;
				if ( watch->scheduled.exchange( true, std::memory_order_acq_rel ) ) break;
			}
		} );
}

std::shared_ptr< DBReactor::Watch > DBReactor::findWatch( DBConnection* connection )
{
	std::lock_guard lock( m_mutex );
	auto found { m_watches.find( connection ) };
	if ( found == m_watches.end() ) return nullptr;
	return found->second;
}

std::shared_ptr< DBReactor::Watch > DBReactor::findOrCreateWatch( std::shared_ptr< DBConnection > connection )
{
	std::lock_guard lock( m_mutex );
	auto [ found, inserted ] { m_watches.try_emplace( connection.get(), std::make_shared< Watch >() ) };
	if ( inserted ) found->second->connection = std::move( connection );
	return found->second;
}

std::shared_ptr< DBReactor::Watch > DBReactor::removeWatch( DBConnection& connection )
{
	std::lock_guard lock( m_mutex );
	auto found { m_watches.find( &connection ) };
	if ( found == m_watches.end() ) return nullptr;
	return std::move( m_watches.extract( found ).mapped() );
}

void DBReactor::registerWatch(
	std::shared_ptr< DBConnection > connection,
	const int socket,
	const SocketInterest interest )
{
	std::lock_guard lock( m_mutex );
	auto [ found, inserted ] { m_watches.try_emplace( connection.get(), std::make_shared< Watch >() ) };
	auto& watch { *found->second };
	if ( inserted ) watch.connection = std::move( connection );
	if ( watch.socket >= 0 && watch.socket != socket ) throw std::logic_error( "Database connection socket changed" );
	const bool unregistered { watch.socket < 0 };
	watch.socket = socket;
	watch.interest = interest;
	epoll_event event {};
	event.events = epollEvents( interest );
	event.data.ptr = watch.connection.get();
	epollControl( m_epoll_fd, inserted || unregistered ? EPOLL_CTL_ADD : EPOLL_CTL_MOD, socket, &event );
}

void DBReactor::updateWatch( DBConnection& connection, const SocketInterest interest )
{
	std::lock_guard lock( m_mutex );
	auto found { m_watches.find( &connection ) };
	if ( found == m_watches.end() || found->second->socket < 0 )
		throw std::logic_error( "Rearming an unwatched database connection" );
	found->second->interest = interest;
	epoll_event event {};
	event.events = epollEvents( interest );
	event.data.ptr = found->second->connection.get();
	epollControl( m_epoll_fd, EPOLL_CTL_MOD, found->second->socket, &event );
}

void DBReactor::run()
{
	std::array< epoll_event, 64 > events {};
	while ( !m_stopping.load( std::memory_order_acquire ) )
	{
		const auto count { epoll_wait( m_epoll_fd, events.data(), static_cast< int >( events.size() ), -1 ) };
		if ( count < 0 )
		{
			if ( errno == EINTR ) continue;
			if ( m_stopping.load( std::memory_order_acquire ) ) return;
			throw std::system_error( errno, std::generic_category(), "epoll_wait failed" );
		}
		for ( std::size_t index = 0; index < static_cast< std::size_t >( count ); ++index )
		{
			auto* connection { static_cast< DBConnection* >( events[ index ].data.ptr ) };
			if ( !connection )
			{
				std::uint64_t ignored;
				while ( ::read( m_wake_fd, &ignored, sizeof( ignored ) ) == -1 && errno == EINTR )
				{}
				continue;
			}
			std::shared_ptr< Watch > watch { findWatch( connection ) };
			if ( !watch ) continue;
			request( watch );
		}
	}
}

void DBReactor::start()
{
	bool expected { false };
	if ( !m_started.compare_exchange_strong( expected, true, std::memory_order_acq_rel ) ) return;
	m_poller = std::jthread( [ this ] { run(); } );
}

void DBReactor::watch( std::shared_ptr< DBConnection > connection, int socket, SocketInterest interest )
{
	if ( !connection || socket < 0 ) throw std::invalid_argument( "Invalid PostgreSQL reactor watch" );
	if ( !m_started.load() || m_stopping.load() ) throw std::logic_error( "Database reactor is not running" );
	registerWatch( std::move( connection ), socket, interest );
}

void DBReactor::rearm( DBConnection& connection, SocketInterest interest )
{
	updateWatch( connection, interest );
}

void DBReactor::unwatch( DBConnection& connection )
{
	std::shared_ptr< Watch > watch { removeWatch( connection ) };
	if ( !watch ) return;
	if ( watch->socket >= 0 && epoll_ctl( m_epoll_fd, EPOLL_CTL_DEL, watch->socket, nullptr ) == -1 && errno != ENOENT )
		throw std::system_error( errno, std::generic_category(), "Unable to remove PostgreSQL socket watch" );
}

void DBReactor::schedule( std::shared_ptr< DBConnection > connection )
{
	if ( !connection ) throw std::invalid_argument( "Cannot schedule a null database connection" );
	std::shared_ptr< Watch > watch { findOrCreateWatch( std::move( connection ) ) };
	request( watch );
}

void DBReactor::stop()
{
	if ( m_stopping.exchange( true, std::memory_order_acq_rel ) ) return;
	notify();
	if ( m_poller.joinable() ) m_poller.join();
	m_workers->stop();
}
} // namespace idhan::db
