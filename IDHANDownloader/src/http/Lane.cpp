#include "http/Lane.hpp"

#include <algorithm>
#include <utility>

#include "HostCallback.hpp"
#include "IDHANDownloader/http/LaneObserver.hpp"
#include "http/IoPool.hpp"
#include "http/LanePolicy.hpp"
#include "http/LaneShard.hpp"

namespace idhan::downloader
{

Lane::Lane( std::string key, LanePolicy& policy, IoPool& pool, const Config config, LaneObserver* observer ) :
  m_key( std::move( key ) ),
  m_policy( policy ),
  m_pool( pool ),
  m_timer_thread( pool.leastLoaded() ),
  m_config( config ),
  m_observer( observer )
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
			if ( request.buffered_sink ) (void)hostCallback( [ & ] { request.buffered_sink->abort(); } );

			callback(
				std::unexpected(
					TransferError { .code = TransferErrorCode::SHUTDOWN, .message = "The lane is shutting down" } ) );
			return;
		}

		m_last_enqueued = std::chrono::steady_clock::now();
		if ( request.progress ) request.progress->stampQueued();

		auto& queue { request.buffered_sink ? m_import_queue : m_queue };
		queue.emplace_back( Pending { .request = std::move( request ), .callback = std::move( callback ) } );
	}

	pump( shared_from_this() );
}

void Lane::cancel( const std::shared_ptr< std::atomic_bool >& cancellation )
{
	std::deque< Pending > cancelled {};
	std::shared_ptr< LaneShard > shard {};

	{
		const std::scoped_lock lock { m_mutex };

		for ( auto* queue : { &m_import_queue, &m_queue } )
		{
			for ( auto entry { queue->begin() }; entry != queue->end(); )
			{
				if ( entry->request.cancellation != cancellation )
				{
					++entry;
					continue;
				}

				cancelled.emplace_back( std::move( *entry ) );
				entry = queue->erase( entry );
			}
		}

		shard = m_shard;
	}

	for ( Pending& pending : cancelled )
	{
		if ( pending.request.buffered_sink ) (void)hostCallback( [ & ] { pending.request.buffered_sink->abort(); } );

		pending.callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::CANCELLED, .message = "Transfer cancelled" } ) );
	}

	if ( shard ) shard->thread().post( [ shard, cancellation ] { shard->cancel( cancellation ); } );

	publish();
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

void Lane::onTransferFinalizing()
{
	publish();
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
	std::deque< Pending > rejected {};
	{
		const std::scoped_lock lock { self.m_mutex };
		const std::size_t ceiling { self.m_policy.concurrency( self.m_config.concurrency ) };

		while ( !self.m_stopped )
		{
			if ( self.m_import_queue.empty() && self.m_queue.empty() ) break;

			const bool disabled { self.m_policy.disabled() };
			if ( !disabled && self.m_in_flight >= ceiling ) break;

			const auto wait {
				disabled ? std::optional { std::chrono::steady_clock::duration::max() } : self.m_policy.claim()
			};
			if ( wait.has_value() )
			{
				if ( wait.value() == std::chrono::steady_clock::duration::max() )
				{
					rejected.swap( self.m_queue );
					for ( auto& pending : self.m_import_queue ) rejected.emplace_back( std::move( pending ) );

					self.m_import_queue.clear();
					self.m_gate.armed = false;
				}
				else
					self.arm( lane, wait.value() );

				break;
			}

			if ( !self.m_shard )
			{
				self.m_shard = std::make_shared< LaneShard >( lane, self.m_pool.leastLoaded(), ceiling );
				auto shard { self.m_shard };
				shard->thread().post( [ shard ] { shard->thread().attach( *shard ); } );
			}

			auto shard { self.m_shard };
			auto& queue { self.m_import_queue.empty() ? self.m_queue : self.m_import_queue };
			Pending pending { std::move( queue.front() ) };
			queue.pop_front();
			if ( pending.request.progress )
			{
				pending.request.progress->phase = NetworkActivity::Phase::DOWNLOADING;
				pending.request.progress->beginDownloading();
			}

			++self.m_in_flight;
			++self.m_dispatched;
			shard->reserve();
			shard->thread().post( [ shard, moved = std::move( pending ) ]() mutable
			                      { shard->start( std::move( moved.request ), std::move( moved.callback ) ); } );
		}
	}
	for ( Pending& pending : rejected )
	{
		if ( pending.request.buffered_sink ) (void)hostCallback( [ & ] { pending.request.buffered_sink->abort(); } );

		pending.callback(
			std::unexpected(
				TransferError {
					.code = TransferErrorCode::RATE_LIMITED,
					.message = "Lane disabled after repeated HTTP 429 responses; enable the lane to retry" } ) );
	}

	// Timer-driven starts and completions must be visible between the pool's periodic sweeps.
	self.publish();
}

void Lane::publish() const
{
	if ( m_observer == nullptr ) return;

	LaneSnapshot snapshot;
	m_policy.fill( snapshot );
	fill( snapshot );
	(void)hostCallback( [ & ] { m_observer->onLaneChanged( snapshot ); } );
}

bool Lane::retirable(
	const std::chrono::steady_clock::duration keep_alive,
	const std::chrono::steady_clock::time_point now ) const
{
	const std::scoped_lock lock { m_mutex };

	return m_queue.empty() && m_import_queue.empty() && m_in_flight == 0 && now - m_last_enqueued >= keep_alive;
}

void Lane::onBytesReceived( const std::size_t bytes )
{
	const std::scoped_lock lock { m_mutex };
	m_downloaded_bytes += bytes;
}

void Lane::fill( LaneSnapshot& snapshot, const std::chrono::steady_clock::time_point now ) const
{
	const std::scoped_lock lock { m_mutex };
	snapshot.in_flight = m_in_flight;
	snapshot.concurrency_limit = m_policy.concurrency( m_config.concurrency );
	snapshot.dispatched = m_dispatched;
	snapshot.dispatch_wait = snapshot.disabled ? "disabled" : "idle";
	if ( !snapshot.disabled && ( !m_queue.empty() || !m_import_queue.empty() ) )
	{
		if ( snapshot.backed_off && snapshot.remaining > std::chrono::steady_clock::duration::zero() )
			snapshot.dispatch_wait = "backoff";
		else if ( m_in_flight >= snapshot.concurrency_limit )
			snapshot.dispatch_wait = "concurrency";
		else if ( snapshot.remaining > std::chrono::steady_clock::duration::zero() )
			snapshot.dispatch_wait = "rate_limit";
		else
			snapshot.dispatch_wait = "ready";
	}
	snapshot.queued = m_queue.size() + m_import_queue.size();
	snapshot.active = !m_stopped;
	snapshot.download_bytes_per_second = m_download_speed.sample( m_downloaded_bytes, now );
}

void Lane::shutdown()
{
	std::deque< Pending > queued {};
	std::shared_ptr< LaneShard > shard {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_stopped ) return;

		m_stopped = true;
		// EventBase teardown can invoke cancelled timer callbacks after the policy is destroyed.
		m_gate.armed = false;
		queued.swap( m_queue );
		for ( auto& pending : m_import_queue ) queued.emplace_back( std::move( pending ) );

		m_import_queue.clear();
		shard = std::move( m_shard );
	}

	for ( Pending& pending : queued )
	{
		if ( pending.request.buffered_sink ) (void)hostCallback( [ & ] { pending.request.buffered_sink->abort(); } );

		pending.callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::SHUTDOWN, .message = "The lane is shutting down" } ) );
	}

	// curl cleanup must run on the shard's IO thread.
	if ( shard )
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
