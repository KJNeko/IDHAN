#include "idhan/http/SSEEventHub.hpp"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace idhan::http
{

static bool matchesType( const std::vector< std::string >& types, const std::string_view type )
{
	if ( types.empty() ) return true;

	for ( const auto& selector : types )
	{
		if ( selector == type ) return true;
		if ( selector.ends_with( ".*" )
		     && type.starts_with( std::string_view { selector }.substr( 0, selector.size() - 1 ) ) )
			return true;
	}

	return false;
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

static std::int64_t nowSeconds()
{
	return std::chrono::duration_cast< std::chrono::seconds >( std::chrono::system_clock::now().time_since_epoch() )
	    .count();
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
		// Leave the registry before tearing down, so a concurrent heartbeatAll() cannot reach a
		// half-destroyed hub. Taking the registry lock without holding m_mutex keeps the order
		// registry-then-hub that heartbeatAll() relies on.
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

SSESubscription SSEEventHub::subscribe(
	std::vector< std::string > types,
	std::vector< ValidatedJson > initial_snapshots,
	const bool include_stored_snapshots )
{
	std::vector< SSEInitialEvent > initial_events {};
	initial_events.reserve( initial_snapshots.size() );
	for ( auto& snapshot : initial_snapshots ) initial_events.push_back( { {}, {}, std::move( snapshot ) } );
	return subscribeEvents( std::move( types ), std::move( initial_events ), include_stored_snapshots );
}

SSESubscription SSEEventHub::subscribeEvents(
	std::vector< std::string > types,
	std::vector< SSEInitialEvent > initial_events,
	const bool include_stored_snapshots )
{
	auto subscriber { std::make_shared< SSESubscriber >() };
	subscriber->types = std::move( types );

	std::lock_guard lock { m_mutex };
	if ( m_shutdown )
	{
		subscriber->pending.emplace_back(
			SSEEvent { SSEEvent::Kind::shutdown, {}, {}, std::nullopt, m_sequence, nowSeconds() } );
		subscriber->closed = true;
		subscriber->readable.post();
		return SSESubscription { *this, std::move( subscriber ) };
	}

	if ( include_stored_snapshots )
	{
		for ( const auto& [ key, snapshot ] : m_snapshots )
		{
			const auto separator { key.find( '\x1f' ) };
			const auto type { key.substr( 0, separator ) };
			const auto entity { separator == std::string::npos ? std::string {} : key.substr( separator + 1 ) };
			if ( !matchesType( subscriber->types, type ) ) continue;

			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::snapshot, type, entity, snapshot, m_sequence, nowSeconds() } );
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
				nowSeconds() } );

	if ( include_stored_snapshots )
	{
		auto marker { ValidatedJson::parse( "{}" ) };
		if ( marker )
			subscriber->pending.emplace_back(
				SSEEvent {
					SSEEvent::Kind::snapshot,
					"stream.snapshot.complete",
					{},
					std::move( *marker ),
					m_sequence,
					nowSeconds() } );
	}
	m_subscribers.emplace_back( subscriber );
	return SSESubscription { *this, std::move( subscriber ) };
}

void SSEEventHub::publish( std::string type, std::string entity, ValidatedJson snapshot, const bool retain_snapshot )
{
	std::lock_guard lock { m_mutex };
	const auto key { snapshotKey( type, entity ) };
	if ( retain_snapshot )
		m_snapshots.insert_or_assign( key, snapshot );
	else
		m_snapshots.erase( key );
	const auto sequence { ++m_sequence };

	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;
		if ( !matchesType( subscriber->types, type ) ) continue;

		const auto pending = std::ranges::find_if(
			subscriber->pending,
			[ &entity, &type ]( const SSEEvent& event )
			{ return event.kind == SSEEvent::Kind::update && event.entity == entity && event.type == type; } );
		if ( pending != subscriber->pending.end() )
		{
			*pending = SSEEvent { SSEEvent::Kind::update, type, entity, snapshot, sequence, nowSeconds() };
		}
		else
		{
			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::update, type, entity, snapshot, sequence, nowSeconds() } );
		}
		subscriber->readable.post();
	}
}

void SSEEventHub::publish( std::string entity, ValidatedJson snapshot )
{
	publish( {}, std::move( entity ), std::move( snapshot ) );
}

void SSEEventHub::remove( std::string type, std::string entity, std::optional< ValidatedJson > removal )
{
	std::lock_guard lock { m_mutex };
	m_snapshots.erase( snapshotKey( type, entity ) );
	const auto sequence { ++m_sequence };

	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;
		if ( !matchesType( subscriber->types, type ) ) continue;

		const auto pending = std::ranges::find_if(
			subscriber->pending,
			[ &entity, &type ]( const SSEEvent& event )
			{ return event.kind == SSEEvent::Kind::update && event.entity == entity && event.type == type; } );
		if ( pending != subscriber->pending.end() )
		{
			*pending = SSEEvent { SSEEvent::Kind::update, type, entity, removal, sequence, nowSeconds() };
		}
		else
		{
			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::update, type, entity, removal, sequence, nowSeconds() } );
		}
		subscriber->readable.post();
	}
}

void SSEEventHub::remove( std::string entity, std::optional< ValidatedJson > removal )
{
	remove( {}, std::move( entity ), std::move( removal ) );
}

void SSEEventHub::heartbeat()
{
	std::lock_guard lock { m_mutex };
	const auto sequence { ++m_sequence };
	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed || !subscriber->pending.empty() ) continue;
		subscriber->pending.emplace_back(
			SSEEvent { SSEEvent::Kind::keepalive, {}, {}, std::nullopt, sequence, nowSeconds() } );
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
		subscriber->pending.emplace_back(
			SSEEvent { SSEEvent::Kind::shutdown, {}, {}, std::nullopt, sequence, nowSeconds() } );
		subscriber->closed = true;
		subscriber->readable.post();
	}
}

} // namespace idhan::http
