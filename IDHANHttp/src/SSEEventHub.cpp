#include "idhan/http/SSEEventHub.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace idhan::http
{

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
	std::vector< std::string > entities,
	std::vector< ValidatedJson > initial_snapshots,
	const bool include_stored_snapshots )
{
	auto subscriber { std::make_shared< SSESubscriber >() };
	subscriber->entities = std::move( entities );

	std::lock_guard lock { m_mutex };
	if ( m_shutdown )
	{
		subscriber->pending.emplace_back(
			SSEEvent { SSEEvent::Kind::shutdown, {}, std::nullopt, m_sequence, nowSeconds() } );
		subscriber->closed = true;
		subscriber->readable.post();
		return SSESubscription { *this, std::move( subscriber ) };
	}

	for ( auto& snapshot : initial_snapshots )
		subscriber->pending.emplace_back(
			SSEEvent { SSEEvent::Kind::snapshot, {}, std::move( snapshot ), m_sequence, nowSeconds() } );

	if ( include_stored_snapshots )
	{
		for ( const auto& [ entity, snapshot ] : m_snapshots )
		{
			if ( !subscriber->entities.empty()
			     && std::ranges::find( subscriber->entities, entity ) == subscriber->entities.end() )
				continue;

			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::snapshot, entity, snapshot, m_sequence, nowSeconds() } );
		}
	}
	m_subscribers.emplace_back( subscriber );
	return SSESubscription { *this, std::move( subscriber ) };
}

void SSEEventHub::publish( std::string entity, ValidatedJson snapshot )
{
	std::lock_guard lock { m_mutex };
	m_snapshots.insert_or_assign( entity, snapshot );
	const auto sequence { ++m_sequence };

	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;
		if ( !subscriber->entities.empty()
		     && std::ranges::find( subscriber->entities, entity ) == subscriber->entities.end() )
			continue;

		const auto pending = std::ranges::find_if(
			subscriber->pending,
			[ &entity ]( const SSEEvent& event )
			{ return event.kind == SSEEvent::Kind::update && event.entity == entity; } );
		if ( pending != subscriber->pending.end() )
		{
			*pending = SSEEvent { SSEEvent::Kind::update, entity, snapshot, sequence, nowSeconds() };
		}
		else
		{
			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::update, entity, snapshot, sequence, nowSeconds() } );
		}
		subscriber->readable.post();
	}
}

void SSEEventHub::remove( std::string entity )
{
	std::lock_guard lock { m_mutex };
	m_snapshots.erase( entity );
	const auto sequence { ++m_sequence };

	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed ) continue;
		if ( !subscriber->entities.empty()
		     && std::ranges::find( subscriber->entities, entity ) == subscriber->entities.end() )
			continue;

		const auto pending = std::ranges::find_if(
			subscriber->pending,
			[ &entity ]( const SSEEvent& event )
			{ return event.kind == SSEEvent::Kind::update && event.entity == entity; } );
		if ( pending != subscriber->pending.end() )
		{
			*pending = SSEEvent { SSEEvent::Kind::update, entity, std::nullopt, sequence, nowSeconds() };
		}
		else
		{
			subscriber->pending.emplace_back(
				SSEEvent { SSEEvent::Kind::update, entity, std::nullopt, sequence, nowSeconds() } );
		}
		subscriber->readable.post();
	}
}

void SSEEventHub::heartbeat()
{
	std::lock_guard lock { m_mutex };
	const auto sequence { ++m_sequence };
	for ( const auto& subscriber : m_subscribers )
	{
		if ( subscriber->closed || !subscriber->pending.empty() ) continue;
		subscriber->pending.emplace_back(
			SSEEvent { SSEEvent::Kind::keepalive, {}, std::nullopt, sequence, nowSeconds() } );
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
			SSEEvent { SSEEvent::Kind::shutdown, {}, std::nullopt, sequence, nowSeconds() } );
		subscriber->closed = true;
		subscriber->readable.post();
	}
}

} // namespace idhan::http
