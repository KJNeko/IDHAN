#include "http/Lane.hpp"

#include <utility>

#include "http/IoPool.hpp"
#include "http/LanePolicy.hpp"
#include "http/LaneShard.hpp"

namespace idhan::downloader
{

Lane::Lane( std::string key, LanePolicy& policy, IoPool& pool, const Config config ) :
  m_key( std::move( key ) ),
  m_policy( policy ),
  m_pool( pool ),
  m_timer_thread( pool.leastLoaded() ),
  m_config( config )
{}

Lane::~Lane()
{
	shutdown();
}

void Lane::submit( TransferRequest request, TransferCallback callback )
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_stopped )
		{
			if ( request.sink ) request.sink->abort();

			callback(
				std::unexpected(
					TransferError { .code = TransferErrorCode::SHUTDOWN, .message = "The lane is shutting down" } ) );
			return;
		}

		m_last_enqueued = std::chrono::steady_clock::now();
		m_queue.emplace_back( Pending { .request = std::move( request ), .callback = std::move( callback ) } );
	}

	pump( shared_from_this() );
}

void Lane::cancel( const std::shared_ptr< std::atomic_bool >& cancellation )
{
	std::deque< Pending > cancelled {};
	std::vector< std::shared_ptr< LaneShard > > shards {};

	{
		const std::scoped_lock lock { m_mutex };

		for ( auto entry { m_queue.begin() }; entry != m_queue.end(); )
		{
			if ( entry->request.cancellation != cancellation )
			{
				++entry;
				continue;
			}

			cancelled.emplace_back( std::move( *entry ) );
			entry = m_queue.erase( entry );
		}

		shards = m_shards;
	}

	for ( Pending& pending : cancelled )
	{
		if ( pending.request.sink ) pending.request.sink->abort();

		pending.callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::CANCELLED, .message = "Transfer cancelled" } ) );
	}

	for ( const auto& shard : shards )
		shard->thread().post( [ shard, cancellation ] { shard->cancel( cancellation ); } );
}

void Lane::wake()
{
	pump( shared_from_this() );
}

void Lane::onTransferFinished()
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_in_flight > 0 ) --m_in_flight;
	}

	pump( shared_from_this() );
}

void Lane::growIfNeeded()
{
	if ( m_shards.empty() )
	{
		auto shard {
			std::make_shared< LaneShard >( weak_from_this(), m_pool.leastLoaded(), m_config.unthrottled_concurrency )
		};
		shard->thread().post( [ shard ] { shard->thread().attach( *shard ); } );
		m_shards.emplace_back( std::move( shard ) );
		return;
	}

	if ( m_policy.throttled() || m_shards.size() >= m_config.max_shards ) return;

	const std::size_t load { m_in_flight + m_queue.size() };

	if ( load <= m_shards.size() * m_config.shard_growth_threshold ) return;

	auto shard {
		std::make_shared< LaneShard >( weak_from_this(), m_pool.leastLoaded(), m_config.unthrottled_concurrency )
	};
	shard->thread().post( [ shard ] { shard->thread().attach( *shard ); } );
	m_shards.emplace_back( std::move( shard ) );
}

std::shared_ptr< LaneShard > Lane::pickShard()
{
	auto* best { &m_shards.front() };

	for ( auto& shard : m_shards )
	{
		if ( shard->inFlight() < ( *best )->inFlight() ) best = &shard;
	}

	return *best;
}

void Lane::arm( const std::shared_ptr< Lane >& lane, const std::chrono::steady_clock::duration wait )
{
	const auto deadline { std::chrono::steady_clock::now() + wait };
	const std::uint64_t generation { m_policy.generation() };

	const bool covered { m_gate.armed && m_gate.generation == generation && m_gate.deadline <= deadline };

	if ( covered ) return;

	m_gate = Gate { .armed = true, .deadline = deadline, .generation = generation };

	m_timer_thread.postAfter(
		wait,
		[ lane, deadline ]
		{
			{
				const std::scoped_lock gate_lock { lane->m_mutex };

				// A nearer wakeup replaced this one; letting it clear the gate would drop that one.
				if ( !lane->m_gate.armed || lane->m_gate.deadline != deadline ) return;

				lane->m_gate.armed = false;
			}

			pump( lane );
		} );
}

void Lane::pump( const std::shared_ptr< Lane >& lane )
{
	Lane& self { *lane };
	const std::scoped_lock lock { self.m_mutex };

	const std::size_t ceiling {
		self.m_policy.concurrency( self.m_config.unthrottled_concurrency, self.m_config.throttled_concurrency )
	};

	while ( !self.m_stopped && !self.m_queue.empty() && self.m_in_flight < ceiling )
	{
		if ( const auto wait { self.m_policy.claim() }; wait.has_value() )
		{
			self.arm( lane, *wait );
			return;
		}

		self.growIfNeeded();

		if ( self.m_shards.empty() ) break;

		auto shard { self.pickShard() };
		Pending pending { std::move( self.m_queue.front() ) };
		self.m_queue.pop_front();
		++self.m_in_flight;

		shard->thread().post( [ shard, moved = std::move( pending ) ]() mutable
		                      { shard->start( std::move( moved.request ), std::move( moved.callback ) ); } );
	}
}

bool Lane::retirable(
	const std::chrono::steady_clock::duration keep_alive,
	const std::chrono::steady_clock::time_point now ) const
{
	const std::scoped_lock lock { m_mutex };

	return m_queue.empty() && m_in_flight == 0 && now - m_last_enqueued >= keep_alive;
}

void Lane::fill( LaneSnapshot& snapshot ) const
{
	const std::scoped_lock lock { m_mutex };
	snapshot.in_flight = m_in_flight;
	snapshot.queued = m_queue.size();
	snapshot.shards = m_shards.size();
	snapshot.active = true;
}

void Lane::shutdown()
{
	std::deque< Pending > queued {};
	std::vector< std::shared_ptr< LaneShard > > shards {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_stopped ) return;

		m_stopped = true;
		queued.swap( m_queue );
		shards.swap( m_shards );
	}

	for ( Pending& pending : queued )
	{
		if ( pending.request.sink ) pending.request.sink->abort();

		pending.callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::SHUTDOWN, .message = "The lane is shutting down" } ) );
	}

	// curl cleanup must run on the shard's IO thread.
	for ( auto& shard : shards )
	{
		IoThread& thread { shard->thread() };
		thread.post(
			[ moved = std::move( shard ) ]() mutable
			{
				moved->thread().detach( *moved );
				moved->shutdown();
				moved.reset();
			} );
	}
}

} // namespace idhan::downloader
