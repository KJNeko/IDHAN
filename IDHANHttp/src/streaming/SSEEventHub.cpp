#include "idhan/http/streaming/SSEEventHub.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace idhan::http
{

std::string_view streamOf( const std::string_view type )
{
	return type.substr( 0, type.find( '.' ) );
}

static bool matchesStream( const SSESubscriber& subscriber, const std::string_view type )
{
	if ( subscriber.streams.empty() ) return true;

	return std::ranges::find( subscriber.streams, streamOf( type ) ) != subscriber.streams.end();
}

static bool matchesSession( const SSESubscriber& subscriber, const std::optional< std::string_view > session )
{
	if ( !session.has_value() ) return true;

	if ( subscriber.session_ids.empty() ) return true;

	return std::ranges::find( subscriber.session_ids, session.value() ) != subscriber.session_ids.end();
}

static std::size_t updateBytes( const SSEEvent& event )
{
	if ( event.kind != SSEEvent::Kind::update ) return 0;

	if ( !event.data.has_value() ) return 0;

	return event.data->text().size();
}

static std::string snapshotKey( const std::string_view type, const std::string_view entity )
{
	return std::string { type } + '\x1f' + std::string { entity };
}

SSESubscription::SSESubscription( SSEEventHub& hub, std::shared_ptr< SSESubscriber > subscriber ) :
  m_subscriber( std::move( subscriber ) ),
  m_hub( &hub )
{}

SSESubscription::SSESubscription( SSESubscription&& other ) noexcept :
  m_subscriber( std::move( other.m_subscriber ) ),
  m_hub( std::exchange( other.m_hub, nullptr ) )
{}

SSESubscription& SSESubscription::operator=( SSESubscription&& other ) noexcept
{
	if ( this == &other ) return *this;

	close();
	m_subscriber = std::move( other.m_subscriber );
	m_hub = std::exchange( other.m_hub, nullptr );
	return *this;
}

SSESubscription::~SSESubscription()
{
	close();
}

runtime::Task< std::optional< SSEEvent > > SSESubscription::next()
{
	while ( m_hub != nullptr && m_subscriber )
	{
		{
			std::lock_guard lock { m_hub->m_mutex };
			if ( !m_subscriber->pending.empty() )
			{
				SSEEvent event { std::move( m_subscriber->pending.front() ) };
				m_subscriber->pending.erase( m_subscriber->pending.begin() );
				m_subscriber->pending_update_bytes -= updateBytes( event );
				co_return event;
			}

			if ( m_subscriber->closed ) co_return std::nullopt;

			// Reset while publication holds the same mutex, so no post can be lost
			// between observing an empty queue and awaiting the baton.
			m_subscriber->readable.reset();
		}

		co_await m_subscriber->readable;
	}

	co_return std::nullopt;
}

void SSESubscription::close() noexcept
{
	if ( m_hub == nullptr || !m_subscriber ) return;

	std::lock_guard lock { m_hub->m_mutex };
	m_subscriber->closed = true;
	m_subscriber->readable.post();
	std::erase( m_hub->m_subscribers, m_subscriber );
	m_hub = nullptr;
}

//! Function-local so a hub constructed during static initialisation still finds the registry built.
static std::mutex& registryMutex()
{
	static std::mutex mutex {};
	return mutex;
}

static std::vector< SSEEventHub* >& registry()
{
	static std::vector< SSEEventHub* > hubs {};
	return hubs;
}

SSEEventHub::SSEEventHub() : m_mutex {}, m_subscribers {}, m_snapshots {}, m_sequence { 0 }, m_shutdown { false }
{
	const std::lock_guard lock { registryMutex() };
	registry().push_back( this );
}

SSEEventHub::~SSEEventHub()
{
	{
		// Unregister under the registry lock alone so heartbeatAll() never reaches a half-destroyed hub.
		const std::lock_guard lock { registryMutex() };
		std::erase( registry(), this );
	}

	shutdown();
}

void SSEEventHub::heartbeatAll()
{
	const std::lock_guard lock { registryMutex() };
	for ( SSEEventHub* hub : registry() ) hub->heartbeat();
}

SSESubscription SSEEventHub::subscribeEvents(
	std::vector< std::string > streams,
	std::vector< SSEInitialEvent > initial_events,
	const bool include_stored_snapshots,
	std::vector< std::string > session_ids )
{
	auto subscriber { std::make_shared< SSESubscriber >() };
	subscriber->streams = std::move( streams );
	subscriber->session_ids = std::move( session_ids );

	std::lock_guard lock { m_mutex };
	if ( m_shutdown )
	{
		subscriber->pending.emplace_back( SSEEvent { SSEEvent::Kind::shutdown, {}, {}, std::nullopt, m_sequence } );
		subscriber->closed = true;
		subscriber->readable.post();
		return SSESubscription { *this, std::move( subscriber ) };
	}

	if ( include_stored_snapshots )
	{
		for ( const auto& [ key, stored ] : m_snapshots )
		{
			const auto separator { key.find( '\x1f' ) };
			const auto type { key.substr( 0, separator ) };
			const auto entity { separator == std::string::npos ? std::string {} : key.substr( separator + 1 ) };
			if ( !matchesStream( *subscriber, type ) ) continue;

			if ( !matchesSession( *subscriber, stored.session ) ) continue;

			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::snapshot, type, entity, stored.data, m_sequence, stored.session } );
		}
	}

	for ( auto& initial : initial_events )
		subscriber->pending.emplace_back(
			SSEEvent {
				SSEEvent::Kind::snapshot,
				std::move( initial.type ),
				std::move( initial.entity ),
				std::move( initial.data ),
				m_sequence,
				std::move( initial.session ) } );

	m_subscribers.emplace_back( subscriber );
	return SSESubscription { *this, std::move( subscriber ) };
}

void SSEEventHub::enqueue( SSESubscriber& subscriber, SSEEvent event, const bool coalesce )
{
	const auto pending = std::ranges::find_if(
		subscriber.pending,
		[ &event ]( const SSEEvent& queued )
		{
			return queued.kind == SSEEvent::Kind::update && queued.entity == event.entity && queued.type == event.type;
		} );

	if ( coalesce && pending != subscriber.pending.end() )
	{
		subscriber.pending_update_bytes -= updateBytes( *pending );
		subscriber.pending_update_bytes += updateBytes( event );
		*pending = std::move( event );
	}
	else
	{
		subscriber.pending_update_bytes += updateBytes( event );
		subscriber.pending.emplace_back( std::move( event ) );
	}

	if ( subscriber.pending_update_bytes > max_pending_update_bytes )
	{
		subscriber.pending.clear();
		subscriber.pending_update_bytes = 0;
		subscriber.closed = true;
	}

	subscriber.readable.post();
}

void SSEEventHub::publish( std::string type, std::string entity, ValidatedJson data, SSEPublishOptions options )
{
	std::lock_guard lock { m_mutex };
	const auto key { snapshotKey( type, entity ) };
	if ( options.retain_snapshot )
		m_snapshots.insert_or_assign( key, StoredSnapshot { data, options.session } );
	else
		m_snapshots.erase( key );
	const auto sequence { ++m_sequence };

	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;

		if ( !matchesStream( *subscriber, type ) ) continue;

		if ( !matchesSession( *subscriber, options.session ) ) continue;

		enqueue(
			*subscriber,
			SSEEvent { SSEEvent::Kind::update, type, entity, data, sequence, options.session },
			options.coalesce );
	}
}

void SSEEventHub::remove( std::string type, std::string entity, std::optional< ValidatedJson > removal )
{
	std::lock_guard lock { m_mutex };
	m_snapshots.erase( snapshotKey( type, entity ) );
	const auto sequence { ++m_sequence };

	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;

		if ( !matchesStream( *subscriber, type ) ) continue;

		enqueue( *subscriber, SSEEvent { SSEEvent::Kind::update, type, entity, removal, sequence }, true );
	}
}

bool SSEEventHub::interested( const std::string_view type, const std::optional< std::string_view > session )
{
	const std::lock_guard lock { m_mutex };
	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;

		if ( !matchesStream( *subscriber, type ) ) continue;

		if ( matchesSession( *subscriber, session ) ) return true;
	}
	return false;
}

std::uint64_t SSESubscription::checkpoint()
{
	const std::lock_guard lock { m_hub->m_mutex };
	return m_hub->m_sequence;
}

void SSESubscription::prependSnapshots(
	std::vector< SSEInitialEvent > snapshots,
	const std::uint64_t checkpoint,
	const std::optional< std::string_view > replaced_stream )
{
	const std::lock_guard lock { m_hub->m_mutex };
	if ( m_subscriber->closed ) return;

	std::erase_if(
		m_subscriber->pending,
		[ this, checkpoint, replaced_stream ]( const SSEEvent& event )
		{
			const bool stale {
				event.kind == SSEEvent::Kind::update && event.sequence <= checkpoint
			    && ( replaced_stream.has_value() ?
			             streamOf( event.type ) == replaced_stream.value() :
			             event.session.has_value() )
			};
			if ( stale ) m_subscriber->pending_update_bytes -= updateBytes( event );
			return stale;
		} );
	std::vector< SSEEvent > initial;
	initial.reserve( snapshots.size() );
	for ( auto& snapshot : snapshots )
		initial.push_back(
			SSEEvent {
				SSEEvent::Kind::snapshot,
				std::move( snapshot.type ),
				std::move( snapshot.entity ),
				std::move( snapshot.data ),
				0,
				std::move( snapshot.session ) } );
	m_subscriber->pending.insert(
		m_subscriber->pending.begin(),
		std::make_move_iterator( initial.begin() ),
		std::make_move_iterator( initial.end() ) );
	m_subscriber->readable.post();
}

void SSEEventHub::heartbeat()
{
	std::lock_guard lock { m_mutex };
	const auto sequence { ++m_sequence };
	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;

		if ( !subscriber->pending.empty() ) continue;

		subscriber->pending.emplace_back( SSEEvent { SSEEvent::Kind::keepalive, {}, {}, std::nullopt, sequence } );
		subscriber->readable.post();
	}
}

void SSEEventHub::shutdown()
{
	std::lock_guard lock { m_mutex };
	if ( m_shutdown ) return;

	m_shutdown = true;
	const auto sequence { ++m_sequence };
	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;

		subscriber->pending.emplace_back( SSEEvent { SSEEvent::Kind::shutdown, {}, {}, std::nullopt, sequence } );
		subscriber->closed = true;
		subscriber->readable.post();
	}
}

} // namespace idhan::http
